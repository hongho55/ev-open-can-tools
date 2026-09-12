package com.hongho55.t2can.gateway;

import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Locale;

final class Esp32HttpClient {
    private static final int CONNECT_TIMEOUT_MS = 4000;
    private static final int READ_TIMEOUT_MS = 15000;
    private static final int MAX_LIST_BYTES = 128 * 1024;
    private static final long MAX_EVENT_BYTES = 8L * 1024L * 1024L;

    private Esp32HttpClient() {
    }

    static String fetchEventList(DiscoveryProtocol.Device device,
                                 SecureStore.Credentials credentials) throws Exception {
        HttpURLConnection connection = open(device.baseUrl() + "/event_list", credentials);
        try {
            requireStatus(connection, HttpURLConnection.HTTP_OK);
            byte[] body = readBounded(connection, MAX_LIST_BYTES);
            return new String(body, StandardCharsets.UTF_8);
        } finally {
            connection.disconnect();
        }
    }

    static File downloadEvent(DiscoveryProtocol.Device device,
                              SecureStore.Credentials credentials,
                              EventRecord event,
                              File target) throws Exception {
        if (event.sha256 == null || event.size < 0 || event.size > MAX_EVENT_BYTES) {
            throw new IllegalArgumentException("event is not verifiable");
        }
        File parent = target.getParentFile();
        if (parent == null || (!parent.exists() && !parent.mkdirs())) {
            throw new IOException("incident directory unavailable");
        }
        File part = new File(target.getPath() + ".part");
        if (part.exists() && !part.delete()) {
            throw new IOException("stale partial file cannot be removed");
        }

        String encodedId = URLEncoder.encode(event.eventId, StandardCharsets.UTF_8.name());
        HttpURLConnection connection = open(device.baseUrl() + "/event_download?id=" + encodedId,
                credentials);
        try {
            requireStatus(connection, HttpURLConnection.HTTP_OK);
            requireHeader(connection, "X-T2CAN-Incident-Id", event.eventId);
            requireHeader(connection, "X-T2CAN-Size", Long.toString(event.size));
            requireHeader(connection, "X-T2CAN-SHA256", event.sha256);
            String etag = connection.getHeaderField("ETag");
            if (etag == null || !etag.replace("\"", "").equalsIgnoreCase(event.sha256)) {
                throw new IOException("incident ETag mismatch");
            }
            int contentLength = connection.getContentLength();
            if (contentLength >= 0 && contentLength != event.size) {
                throw new IOException("incident Content-Length mismatch");
            }

            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            long total = 0;
            try (BufferedInputStream input = new BufferedInputStream(connection.getInputStream());
                 FileOutputStream output = new FileOutputStream(part, false)) {
                byte[] buffer = new byte[64 * 1024];
                int read;
                while ((read = input.read(buffer)) != -1) {
                    total += read;
                    if (total > MAX_EVENT_BYTES || total > event.size) {
                        throw new IOException("incident exceeds declared size");
                    }
                    digest.update(buffer, 0, read);
                    output.write(buffer, 0, read);
                }
                output.flush();
                output.getFD().sync();
            }
            if (total != event.size || !digestHex(digest).equalsIgnoreCase(event.sha256)) {
                throw new IOException("incident body hash mismatch");
            }
            if (target.exists()) {
                FileHash existing = hashFile(target, MAX_EVENT_BYTES);
                if (existing.size != event.size
                        || !existing.sha256.equalsIgnoreCase(event.sha256)) {
                    throw new IOException("incident local content conflict");
                }
                if (!part.delete()) {
                    throw new IOException("duplicate partial cleanup failed");
                }
                return target;
            }
            if (!part.renameTo(target)) {
                throw new IOException("incident atomic rename failed");
            }
            FileHash readBack = hashFile(target, MAX_EVENT_BYTES);
            if (readBack.size != event.size || !readBack.sha256.equalsIgnoreCase(event.sha256)) {
                if (!target.delete()) {
                    throw new IOException("invalid incident read-back could not be removed");
                }
                throw new IOException("incident read-back failed");
            }
            return target;
        } finally {
            connection.disconnect();
            if (part.exists() && !part.delete()) {
                // Do not turn a verified final file into a false success if cleanup failed.
                throw new IOException("incident partial cleanup failed");
            }
        }
    }

    static void ackEvent(DiscoveryProtocol.Device device,
                         SecureStore.Credentials credentials,
                         EventRecord event) throws Exception {
        if (event.sha256 == null || event.size < 0 || event.size > MAX_EVENT_BYTES) {
            throw new IllegalArgumentException("event is not verifiable");
        }
        String body = "id=" + URLEncoder.encode(event.eventId, StandardCharsets.UTF_8.name())
                + "&size=" + URLEncoder.encode(Long.toString(event.size), StandardCharsets.UTF_8.name())
                + "&sha256=" + URLEncoder.encode(event.sha256, StandardCharsets.UTF_8.name());
        byte[] encoded = body.getBytes(StandardCharsets.US_ASCII);
        HttpURLConnection connection = open(device.baseUrl() + "/event_ack", credentials, "POST");
        connection.setDoOutput(true);
        connection.setFixedLengthStreamingMode(encoded.length);
        connection.setRequestProperty("Content-Type", "application/x-www-form-urlencoded");
        connection.setRequestProperty("Cache-Control", "no-store");
        try {
            try (OutputStream output = connection.getOutputStream()) {
                output.write(encoded);
            }
            requireStatus(connection, HttpURLConnection.HTTP_OK);
            JSONObject response = new JSONObject(new String(
                    readBounded(connection, 16 * 1024), StandardCharsets.UTF_8));
            if (!response.optBoolean("ok", false)) {
                throw new IOException("incident ACK was not confirmed");
            }
        } finally {
            connection.disconnect();
        }
    }

    private static HttpURLConnection open(String url, SecureStore.Credentials credentials)
            throws IOException {
        return open(url, credentials, "GET");
    }

    private static HttpURLConnection open(String url, SecureStore.Credentials credentials,
                                          String method) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
        connection.setReadTimeout(READ_TIMEOUT_MS);
        connection.setInstanceFollowRedirects(false);
        connection.setRequestMethod(method);
        String auth = credentials.username + ":" + credentials.password;
        String encoded = android.util.Base64.encodeToString(
                auth.getBytes(StandardCharsets.UTF_8), android.util.Base64.NO_WRAP);
        connection.setRequestProperty("Authorization", "Basic " + encoded);
        connection.setRequestProperty("Cache-Control", "no-store");
        return connection;
    }

    private static void requireStatus(HttpURLConnection connection, int expected) throws IOException {
        int status = connection.getResponseCode();
        if (status != expected) {
            throw new IOException("ESP32 HTTP status " + status);
        }
    }

    private static void requireHeader(HttpURLConnection connection, String name, String expected)
            throws IOException {
        String actual = connection.getHeaderField(name);
        if (actual == null || !actual.equalsIgnoreCase(expected)) {
            throw new IOException(name + " mismatch");
        }
    }

    private static byte[] readBounded(HttpURLConnection connection, int maxBytes) throws IOException {
        try (BufferedInputStream input = new BufferedInputStream(connection.getInputStream())) {
            byte[] output = new byte[maxBytes + 1];
            int total = 0;
            int read;
            while (total < output.length && (read = input.read(output, total, output.length - total)) != -1) {
                total += read;
            }
            if (total > maxBytes) {
                throw new IOException("ESP32 response too large");
            }
            byte[] exact = new byte[total];
            System.arraycopy(output, 0, exact, 0, total);
            return exact;
        }
    }

    private static String digestHex(MessageDigest digest) {
        StringBuilder output = new StringBuilder(64);
        for (byte value : digest.digest()) {
            output.append(String.format(Locale.US, "%02x", value & 0xff));
        }
        return output.toString();
    }

    private static FileHash hashFile(File file, long maxBytes) throws IOException {
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
                    throw new IOException("local incident too large");
                }
                digest.update(buffer, 0, read);
            }
        }
        String hex = digestHex(digest);
        return new FileHash(total, hex);
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
