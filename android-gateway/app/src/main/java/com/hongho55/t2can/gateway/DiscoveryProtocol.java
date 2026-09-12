package com.hongho55.t2can.gateway;

import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class DiscoveryProtocol {
    static final int PORT = 36991;
    static final byte[] REQUEST = "T2CAN_DISCOVER_V1".getBytes(StandardCharsets.US_ASCII);
    private static final int RESPONSE_BYTES = 512;
    private static final Pattern SCHEMA = stringField("schema");
    private static final Pattern SERVICE = stringField("service");
    private static final Pattern HTTP_PORT = scalarField("port", "([0-9]+)");
    private static final Pattern READ_ONLY = scalarField("readOnly", "(true|false)");

    private DiscoveryProtocol() {
    }

    static Device parseResponse(byte[] payload, InetAddress sender) {
        if (payload.length == 0 || payload.length > RESPONSE_BYTES) {
            throw new IllegalArgumentException("discovery response size");
        }
        String json = new String(payload, StandardCharsets.UTF_8).trim();
        if (!json.startsWith("{") || !json.endsWith("}")) {
            throw new IllegalArgumentException("discovery response shape");
        }
        if (!"t2can-discovery-v1".equals(required(SCHEMA, json))
                || !"EVCANTool".equals(required(SERVICE, json))
                || Integer.parseInt(required(HTTP_PORT, json)) != 80
                || !"true".equals(required(READ_ONLY, json))) {
            throw new IllegalArgumentException("unsupported discovery response");
        }
        if (sender == null || sender.isAnyLocalAddress() || sender.isLoopbackAddress()
                || (!sender.isSiteLocalAddress() && !sender.isLinkLocalAddress())) {
            throw new IllegalArgumentException("invalid discovery sender");
        }
        return new Device(sender, 80);
    }

    private static Pattern stringField(String name) {
        return Pattern.compile("\\\"" + name + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    }

    private static Pattern scalarField(String name, String valuePattern) {
        return Pattern.compile("\\\"" + name + "\\\"\\s*:\\s*" + valuePattern);
    }

    private static String required(Pattern pattern, String json) {
        Matcher matcher = pattern.matcher(json);
        if (!matcher.find()) {
            throw new IllegalArgumentException("missing or duplicate discovery field");
        }
        String value = matcher.group(1);
        if (matcher.find()) {
            throw new IllegalArgumentException("missing or duplicate discovery field");
        }
        return value;
    }

    static final class Device {
        final InetAddress address;
        final int httpPort;

        Device(InetAddress address, int httpPort) {
            this.address = address;
            this.httpPort = httpPort;
        }

        String baseUrl() {
            return "http://" + address.getHostAddress() + ":" + httpPort;
        }
    }
}
