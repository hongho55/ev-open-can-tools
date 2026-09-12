package com.hongho55.t2can.gateway;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

import java.nio.charset.StandardCharsets;

import org.junit.Test;

public class MacUploadClientTest {
    private static final String HASH = "9d6f965ac832e0a4e4b5e3f7b7e4e6b2"
            + "2e6b7e4b4f3f8b2f5d0b4f8f2f0f6a1c";

    @Test
    public void acceptsOnlyDurableStoredResponsesForTheExactItem() throws Exception {
        LocalQueueDb.Item item = item(HASH);
        MacUploadClient.UploadResult stored = MacUploadClient.parseResult(
                ("{\"ok\":true,\"status\":\"stored\",\"id\":\"1-0\","
                        + "\"sha256\":\"" + HASH + "\"}").getBytes(StandardCharsets.UTF_8), item);
        assertEquals("stored", stored.status);

        MacUploadClient.UploadResult duplicate = MacUploadClient.parseResult(
                ("{\"ok\":true,\"status\":\"already_stored\",\"id\":\"1-0\","
                        + "\"sha256\":\"" + HASH.toUpperCase() + "\"}")
                        .getBytes(StandardCharsets.UTF_8), item);
        assertEquals("already_stored", duplicate.status);
    }

    @Test
    public void rejectsResponseForAnotherEventOrHash() throws Exception {
        LocalQueueDb.Item item = item(HASH);
        try {
            MacUploadClient.parseResult(
                    ("{\"ok\":true,\"status\":\"stored\",\"id\":\"2-0\","
                            + "\"sha256\":\"" + HASH + "\"}")
                            .getBytes(StandardCharsets.UTF_8), item);
            fail("response for another event must be rejected");
        } catch (MacUploadClient.UploadException error) {
            assertEquals("mac_commit_not_confirmed", error.code);
        }
    }

    @Test
    public void rejectsNonDurableStatus() throws Exception {
        LocalQueueDb.Item item = item(HASH);
        try {
            MacUploadClient.parseResult(
                    ("{\"ok\":true,\"status\":\"accepted\",\"id\":\"1-0\","
                            + "\"sha256\":\"" + HASH + "\"}")
                            .getBytes(StandardCharsets.UTF_8), item);
            fail("transport acceptance must not be treated as commit");
        } catch (MacUploadClient.UploadException error) {
            assertEquals("mac_commit_not_durable", error.code);
        }
    }

    private static LocalQueueDb.Item item(String hash) {
        return new LocalQueueDb.Item(
                "board-test", "1-0", 7L, hash, "/does/not/exist", "UPLOADING", 1,
                "android-gateway", 123L, 0L, null);
    }
}
