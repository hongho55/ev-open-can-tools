package com.hongho55.t2can.gateway;

import static org.junit.Assert.assertEquals;

import java.net.InetAddress;
import java.nio.charset.StandardCharsets;

import org.junit.Test;

public class DiscoveryProtocolTest {
    @Test
    public void acceptsOnlyTheVersionedReadOnlyResponse() throws Exception {
        String response = "{\"schema\":\"t2can-discovery-v1\","
                + "\"service\":\"EVCANTool\",\"port\":80,\"readOnly\":true}";
        DiscoveryProtocol.Device device = DiscoveryProtocol.parseResponse(
                response.getBytes(StandardCharsets.UTF_8),
                InetAddress.getByName("192.168.43.2"));

        assertEquals("192.168.43.2", device.address.getHostAddress());
        assertEquals(80, device.httpPort);
        assertEquals("http://192.168.43.2:80", device.baseUrl());
    }

    @Test(expected = IllegalArgumentException.class)
    public void rejectsControlCapableOrUnknownResponses() throws Exception {
        DiscoveryProtocol.parseResponse(
                "{\"schema\":\"t2can-discovery-v1\",\"service\":\"EVCANTool\","
                        .concat("\"port\":80,\"readOnly\":false}")
                        .getBytes(StandardCharsets.UTF_8),
                InetAddress.getByName("192.168.43.2"));
    }

    @Test(expected = IllegalArgumentException.class)
    public void rejectsPublicSenders() throws Exception {
        DiscoveryProtocol.parseResponse(
                "{\"schema\":\"t2can-discovery-v1\",\"service\":\"EVCANTool\","
                        .concat("\"port\":80,\"readOnly\":true}")
                        .getBytes(StandardCharsets.UTF_8),
                InetAddress.getByName("8.8.8.8"));
    }
}
