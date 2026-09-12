package com.hongho55.t2can.gateway;

import android.content.Context;

import java.io.File;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

final class TransferEngine {
    private static final long MAX_EVENT_BYTES = 8L * 1024L * 1024L;
    private static final long UPLOAD_LEASE_MS = 2L * 60L * 1000L;
    private static final String LEASE_OWNER = "android-gateway";

    private TransferEngine() {
    }

    static void runOnce(Context context) {
        Context appContext = context.getApplicationContext();
        SecureStore.Credentials credentials;
        try {
            credentials = SecureStore.load(appContext);
        } catch (Exception error) {
            GatewayStatus.write(appContext, "secure_store_error");
            return;
        }
        if (credentials == null || !credentials.recorderReady()) {
            GatewayStatus.write(appContext, "credentials_missing");
            return;
        }

        int queued = 0;
        int skipped = 0;
        int uploaded = 0;
        int acked = 0;
        int failures = 0;
        boolean discoveryFoundNothing = false;
        List<DiscoveryProtocol.Device> devices;
        try {
            devices = DiscoveryClient.discover();
            discoveryFoundNothing = devices.isEmpty();
        } catch (Exception error) {
            devices = List.of();
            failures++;
        }

        Map<String, DiscoveryProtocol.Device> devicesByBoard = new HashMap<>();
        try (LocalQueueDb queue = new LocalQueueDb(appContext)) {
            for (DiscoveryProtocol.Device device : devices) {
                try {
                    EventListParser.ParsedList list = EventListParser.parse(
                            Esp32HttpClient.fetchEventList(device, credentials));
                    devicesByBoard.putIfAbsent(list.boardId, device);
                    File directory = incidentDirectory(appContext, list.boardId);
                    for (EventRecord event : list.events) {
                        if (event.acknowledged || event.sha256 == null
                                || event.size > MAX_EVENT_BYTES) {
                            skipped++;
                            continue;
                        }
                        LocalQueueDb.Item existing = queue.find(list.boardId, event.eventId);
                        if (existing != null && !"QUEUED_LOCAL".equals(existing.state)) {
                            skipped++;
                            continue;
                        }
                        File target = new File(directory, "incident-" + event.eventId + ".jsonl");
                        try {
                            Esp32HttpClient.downloadEvent(device, credentials, event, target);
                            queue.enqueue(list.boardId, event, target.getAbsolutePath(),
                                    System.currentTimeMillis());
                            queued++;
                        } catch (Exception error) {
                            failures++;
                        }
                    }
                } catch (Exception error) {
                    failures++;
                }
            }

            if (credentials.uploadReady()) {
                for (;;) {
                    LocalQueueDb.Item item = queue.claimUpload(
                            LEASE_OWNER, System.currentTimeMillis(), UPLOAD_LEASE_MS);
                    if (item == null) {
                        break;
                    }
                    try {
                        MacUploadClient.UploadResult result = MacUploadClient.upload(item, credentials);
                        queue.markMacCommitted(item, result.status, System.currentTimeMillis());
                        uploaded++;
                    } catch (Exception error) {
                        failures++;
                        try {
                            queue.markUploadFailure(item, errorCode(error), System.currentTimeMillis());
                        } catch (Exception ignored) {
                            // The lease remains recoverable after its bounded expiry.
                        }
                    }
                }
            }

            for (LocalQueueDb.Item item : queue.pendingAcks()) {
                DiscoveryProtocol.Device device = devicesByBoard.get(item.boardId);
                if (device == null) {
                    continue;
                }
                try {
                    EventRecord event = new EventRecord(item.eventId, item.size, item.sha256, false);
                    Esp32HttpClient.ackEvent(device, credentials, event);
                    queue.markAcked(item, System.currentTimeMillis());
                    acked++;
                } catch (Exception error) {
                    failures++;
                }
            }
        } catch (Exception error) {
            GatewayStatus.write(appContext, "queue_error");
            return;
        }

        StringBuilder status = new StringBuilder("sync_q")
                .append(queued)
                .append("_u")
                .append(uploaded)
                .append("_a")
                .append(acked)
                .append("_s")
                .append(skipped);
        if (!credentials.uploadReady()) {
            status.append("_upload_not_configured");
        }
        if (discoveryFoundNothing) {
            status.append("_no_device");
        }
        if (failures > 0) {
            status.append("_e").append(failures);
        }
        GatewayStatus.write(appContext, status.toString());
    }

    private static String errorCode(Exception error) {
        if (error instanceof MacUploadClient.UploadException) {
            return ((MacUploadClient.UploadException) error).code;
        }
        return "upload_failed";
    }

    private static File incidentDirectory(Context context, String boardId) throws Exception {
        String safeBoard = boardId.replaceAll("[^A-Za-z0-9_.-]", "_");
        File directory = new File(new File(context.getFilesDir(), "incidents"), "board-" + safeBoard);
        if (!directory.exists() && !directory.mkdirs()) {
            throw new Exception("incident directory unavailable");
        }
        return directory;
    }
}
