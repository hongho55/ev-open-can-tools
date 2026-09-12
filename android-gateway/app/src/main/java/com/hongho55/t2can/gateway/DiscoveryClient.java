package com.hongho55.t2can.gateway;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketTimeoutException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

final class DiscoveryClient {
    private static final int RESPONSE_BYTES = 512;
    private static final int TIMEOUT_MS = 250;
    private static final int TOTAL_TIMEOUT_MS = 1500;

    private DiscoveryClient() {
    }

    static List<DiscoveryProtocol.Device> discover() throws IOException {
        Map<String, DiscoveryProtocol.Device> devices = new LinkedHashMap<>();
        long deadline = System.currentTimeMillis() + TOTAL_TIMEOUT_MS;
        try (DatagramSocket socket = new DatagramSocket()) {
            socket.setBroadcast(true);
            socket.setSoTimeout(TIMEOUT_MS);
            InetAddress broadcast = InetAddress.getByName("255.255.255.255");
            socket.send(new DatagramPacket(
                    DiscoveryProtocol.REQUEST,
                    DiscoveryProtocol.REQUEST.length,
                    broadcast,
                    DiscoveryProtocol.PORT));

            while (System.currentTimeMillis() < deadline && devices.size() < 4) {
                byte[] bytes = new byte[RESPONSE_BYTES];
                DatagramPacket packet = new DatagramPacket(bytes, bytes.length);
                try {
                    socket.receive(packet);
                } catch (SocketTimeoutException ignored) {
                    continue;
                }
                try {
                    DiscoveryProtocol.Device device = DiscoveryProtocol.parseResponse(
                            Arrays.copyOf(packet.getData(), packet.getLength()), packet.getAddress());
                    devices.put(device.address.getHostAddress(), device);
                } catch (Exception ignored) {
                    // Discovery is unauthenticated by design; malformed peer input is ignored.
                }
            }
        }
        return new ArrayList<>(devices.values());
    }
}
