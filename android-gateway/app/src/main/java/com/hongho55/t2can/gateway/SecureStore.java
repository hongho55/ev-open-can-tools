package com.hongho55.t2can.gateway;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;

import java.nio.charset.StandardCharsets;
import java.net.URI;
import java.security.KeyStore;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

final class SecureStore {
    private static final String PREFS = "gateway_secure";
    private static final String CREDENTIALS = "recorder_credentials";
    private static final String UPLOAD_TOKEN = "upload_token";
    private static final String KEY_ALIAS = "t2can_gateway_aes_v1";
    private static final String UPLOAD_URL = "upload_url";

    private SecureStore() {
    }

    static void save(Context context, String username, String password,
                     String uploadUrl, String uploadToken) throws Exception {
        if (username == null || username.length() > 64 || hasHeaderControl(username)
                || password == null || password.length() > 128 || hasHeaderControl(password)) {
            throw new IllegalArgumentException("invalid recorder credentials");
        }
        if (uploadUrl == null || uploadUrl.length() > 256 || hasHeaderControl(uploadUrl)
                || (!uploadUrl.isEmpty() && !validUploadUrl(uploadUrl))
                || uploadToken == null || uploadToken.length() > 256
                || hasHeaderControl(uploadToken)) {
            throw new IllegalArgumentException("invalid upload settings");
        }
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        prefs.edit()
                .putString(CREDENTIALS, encrypt(username + "\u0000" + password))
                .putString(UPLOAD_TOKEN, encrypt(uploadToken))
                .putString(UPLOAD_URL, uploadUrl)
                .apply();
    }

    static Credentials load(Context context) throws Exception {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String encodedCredentials = prefs.getString(CREDENTIALS, null);
        String encodedToken = prefs.getString(UPLOAD_TOKEN, null);
        if (encodedCredentials == null) {
            return null;
        }
        String[] values = decrypt(encodedCredentials).split("\u0000", -1);
        if (values.length != 2) {
            throw new IllegalStateException("stored credentials format");
        }
        String token = encodedToken == null ? "" : decrypt(encodedToken);
        return new Credentials(values[0], values[1],
                prefs.getString(UPLOAD_URL, ""), token);
    }

    private static String encrypt(String plaintext) throws Exception {
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.ENCRYPT_MODE, key(), new GCMParameterSpec(128, randomIv()));
        byte[] iv = cipher.getIV();
        byte[] ciphertext = cipher.doFinal(plaintext.getBytes(StandardCharsets.UTF_8));
        byte[] combined = new byte[iv.length + ciphertext.length];
        System.arraycopy(iv, 0, combined, 0, iv.length);
        System.arraycopy(ciphertext, 0, combined, iv.length, ciphertext.length);
        return Base64.encodeToString(combined, Base64.NO_WRAP);
    }

    private static String decrypt(String encoded) throws Exception {
        byte[] combined = Base64.decode(encoded, Base64.NO_WRAP);
        if (combined.length <= 12) {
            throw new IllegalStateException("stored secret format");
        }
        byte[] iv = new byte[12];
        byte[] ciphertext = new byte[combined.length - iv.length];
        System.arraycopy(combined, 0, iv, 0, iv.length);
        System.arraycopy(combined, iv.length, ciphertext, 0, ciphertext.length);
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(128, iv));
        return new String(cipher.doFinal(ciphertext), StandardCharsets.UTF_8);
    }

    private static SecretKey key() throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        if (store.containsAlias(KEY_ALIAS)) {
            return ((SecretKey) store.getKey(KEY_ALIAS, null));
        }
        KeyGenerator generator = KeyGenerator.getInstance(
                KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        generator.init(new KeyGenParameterSpec.Builder(KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .build());
        return generator.generateKey();
    }

    private static byte[] randomIv() {
        byte[] iv = new byte[12];
        new java.security.SecureRandom().nextBytes(iv);
        return iv;
    }

    static final class Credentials {
        final String username;
        final String password;
        final String uploadUrl;
        final String uploadToken;

        Credentials(String username, String password, String uploadUrl, String uploadToken) {
            this.username = username;
            this.password = password;
            this.uploadUrl = uploadUrl;
            this.uploadToken = uploadToken;
        }

        boolean recorderReady() {
            return !username.isEmpty() && !password.isEmpty();
        }

        boolean uploadReady() {
            return validUploadUrl(uploadUrl) && !uploadToken.isEmpty();
        }
    }

    private static boolean hasHeaderControl(String value) {
        for (int i = 0; i < value.length(); i++) {
            char character = value.charAt(i);
            if (character == '\r' || character == '\n' || character < 0x20) {
                return true;
            }
        }
        return false;
    }

    private static boolean validUploadUrl(String raw) {
        if (raw == null || raw.isEmpty()) {
            return false;
        }
        try {
            URI uri = new URI(raw);
            return "https".equalsIgnoreCase(uri.getScheme())
                    && uri.getHost() != null
                    && uri.getUserInfo() == null
                    && uri.getQuery() == null
                    && uri.getFragment() == null
                    && uri.getPath() != null
                    && !uri.getPath().isEmpty();
        } catch (Exception error) {
            return false;
        }
    }
}
