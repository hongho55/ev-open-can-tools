package com.hongho55.t2can.gateway;

import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URI;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Locale;

final class MacUploadClient {
    private static final int CONNECT_TIMEOUT_MS = 5000;
    private static final int READ_TIMEOUT_MS = 30000;
    private static final int MAX_RESPONSE_BYTES = 16 * 1024;
    private static final long MAX_EVENT_BYTES = 8L * 1024L * 1024L;

    private MacUploadClient() {
    }

    static UploadResult upload(LocalQueueDb.Item item, SecureStore.Credentials credentials)
            throws Exception {
        if (item == null || credentials == null || !credentials.uploadReady()) {
            throw new UploadException("upload_not_configured");
        }
        File file = new File(item.localPath);
        FileHash before = hashFile(file, MAX_EVENT_BYTES);
        if (before.size != item.size || !before.sha256.equalsIgnoreCase(item.sha256)) {
            throw new UploadException("local_file_changed");
        }

        URL endpoint = endpoint(credentials.uploadUrl);
        HttpURLConnection connection = (HttpURLConnection) endpoint.openConnection();
        connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
        connection.setReadTimeout(READ_TIMEOUT_MS);
        connection.setInstanceFollowRedirects(false);
        connection.setRequestMethod("POST");
        connection.setDoOutput(true);
        connection.setFixedLengthStreamingMode(item.size);
        connection.setRequestProperty("Authorization", "Bearer " + credentials.uploadToken);
        connection.setRequestProperty("Content-Type", "application/x-ndjson");
        connection.setRequestProperty("Cache-Control", "no-store");
        connection.setRequestProperty("X-T2CAN-Incident-Id", item.eventId);
        connection.setRequestProperty("X-T2CAN-Board-Id", item.boardId);
        connection.setRequestProperty("X-T2CAN-Size", Long.toString(item.size));
        connection.setRequestProperty("X-T2CAN-SHA256", item.sha256);

        try (OutputStream output = connection.getOutputStream()) {
            streamFile(file, item, output);
            int status = connection.getResponseCode();
            InputStream responseStream = status >= 200 && status < 300
                    ? connection.getInputStream() : connection.getErrorStream();
            byte[] response = readBounded(responseStream, MAX_RESPONSE_BYTES);
            if (status != HttpURLConnection.HTTP_OK && status != HttpURLConnection.HTTP_CREATED) {
                throw new UploadException("mac_http_" + status);
            }
            return parseResult(response, item);
        } finally {
            connection.disconnect();
        }
    }

    private static URL endpoint(String raw) throws Exception {
        URI uri = new URI(raw);
        if (!"https".equalsIgnoreCase(uri.getScheme())
                || uri.getHost() == null
                || uri.getUserInfo() != null
                || uri.getQuery() != null
                || uri.getFragment() != null
                || uri.getPath() == null
                || uri.getPath().isEmpty()) {
            throw new UploadException("invalid_upload_endpoint");
        }
        return uri.toURL();
    }

    private static void streamFile(File file, LocalQueueDb.Item item, OutputStream output)
            throws Exception {
        long total = 0;
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (BufferedInputStream input = new BufferedInputStream(new FileInputStream(file))) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = input.read(buffer)) != -1) {
                total += read;
                if (total > MAX_EVENT_BYTES || total > item.size) {
                    throw new UploadException("local_file_size_changed");
                }
                digest.update(buffer, 0, read);
                output.write(buffer, 0, read);
            }
            output.flush();
        }
        if (total != item.size || !digestHex(digest).equalsIgnoreCase(item.sha256)) {
            throw new UploadException("local_file_changed");
        }
    }

    static UploadResult parseResult(byte[] response, LocalQueueDb.Item item)
            throws Exception {
        final JSONObject payload;
        try {
            payload = new JSONObject(new String(response, StandardCharsets.UTF_8));
        } catch (Exception error) {
            throw new UploadException("mac_invalid_response");
        }
        if (!payload.optBoolean("ok", false)
                || !item.eventId.equals(payload.optString("id", ""))
                || !item.sha256.equalsIgnoreCase(payload.optString("sha256", ""))) {
            throw new UploadException("mac_commit_not_confirmed");
        }
        String status = payload.optString("status", "");
        if (!"stored".equals(status) && !"already_stored".equals(status)) {
            throw new UploadException("mac_commit_not_durable");
        }
        return new UploadResult(status);
    }

    private static byte[] readBounded(InputStream input, int maxBytes) throws IOException {
        if (input == null) {
            return new byte[0];
        }
        try (InputStream source = input) {
            byte[] output = new byte[maxBytes + 1];
            int total = 0;
            int read;
            while (total < output.length
                    && (read = source.read(output, total, output.length - total)) != -1) {
                total += read;
            }
            if (total > maxBytes) {
                throw new IOException("response too large");
            }
            byte[] exact = new byte[total];
            System.arraycopy(output, 0, exact, 0, total);
            return exact;
        }
    }

    private static FileHash hashFile(File file, long maxBytes) throws IOException {
        if (!file.isFile()) {
            throw new UploadException("local_file_missing");
        }
        MessageDigest digest;
        try {
            digest = MessageDigest.getInstance("SHA-256");
        } catch (Exception error) {
            throw new IOException("SHA-256 unavailable", error);
        }
        long total = 0;
        try (FileInputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = input.read(buffer)) != -1) {
                total += read;
                if (total > maxBytes) {
                    throw new UploadException("local_file_too_large");
                }
                digest.update(buffer, 0, read);
            }
        }
        return new FileHash(total, digestHex(digest));
    }

    private static String digestHex(MessageDigest digest) {
        StringBuilder output = new StringBuilder(64);
        for (byte value : digest.digest()) {
            output.append(String.format(Locale.US, "%02x", value & 0xff));
        }
        return output.toString();
    }

    static final class UploadResult {
        final String status;

        UploadResult(String status) {
            this.status = status;
        }
    }

    static final class UploadException extends IOException {
        final String code;

        UploadException(String code) {
            super(code);
            this.code = code;
        }
    }

    private static final class FileHash {
        final long size;
        final String sha256;

        FileHash(long size, String sha256) {
            this.size = size;
            this.sha256 = sha256;
        }
    }
}
