#pragma once

// BLE (NimBLE) app interface.
//
// Exposes a small GATT service so a phone app can talk to the device over
// Bluetooth LE, mirroring the HTTP dashboard. Gated by -DBLE_APP (only wired
// on BT-capable envs) and compiled out of native builds.
//
// GATT layout (Nordic-UART style):
//   Service   e7c10001-9a3f-4bd2-b1a7-0c0ffee00001
//   Command   e7c10002-...  WRITE       app -> device, newline-framed JSON
//   Response  e7c10003-...  NOTIFY      device -> app, newline-framed JSON
//
// A written command is a JSON object {"cmd":"...","args":{...}} terminated by
// '\n'. Supported read-only maintenance commands include `status`, `stats`, and
// `snapshot`; the snapshot has the versioned schema used by the Mac collector.
// Writes are accumulated until that newline arrives, so a command longer
// than one ATT write is simply split by the client -- but a SINGLE write may not
// exceed 255 bytes (see the flat buffer in bleCmdWriteCb).
//
// The reply is fetched by paged READs of the Response characteristic, because a
// GATT attribute is capped at 512 bytes and replies can be larger.
//
// The characteristics require an encrypted, authenticated LE Secure Connections
// bond. This is transport security, not device-owner authorization; state-changing
// commands remain blocked until the separate owner boundary is implemented.

#if defined(BLE_APP) && !defined(NATIVE_BUILD) && defined(CONFIG_BT_NIMBLE_ENABLED)

#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Provided by the dashboard (include order: app.h pulls the dashboard first).
static String dashBuildBleStatusJson();

static const char *kBleTag = "ble";
static const char *kBleDeviceName = "EVCANTool";

// 128-bit UUIDs, stored least-significant-byte first (NimBLE convention).
static const ble_uuid128_t bleSvcUuid = BLE_UUID128_INIT(
    0x01, 0x00, 0xe0, 0xfe, 0x0f, 0x0c, 0xa7, 0xb1,
    0xd2, 0x4b, 0x3f, 0x9a, 0x01, 0x00, 0xc1, 0xe7);
static const ble_uuid128_t bleCmdUuid = BLE_UUID128_INIT(
    0x01, 0x00, 0xe0, 0xfe, 0x0f, 0x0c, 0xa7, 0xb1,
    0xd2, 0x4b, 0x3f, 0x9a, 0x02, 0x00, 0xc1, 0xe7);
static const ble_uuid128_t bleRespUuid = BLE_UUID128_INIT(
    0x01, 0x00, 0xe0, 0xfe, 0x0f, 0x0c, 0xa7, 0xb1,
    0xd2, 0x4b, 0x3f, 0x9a, 0x03, 0x00, 0xc1, 0xe7);

static uint16_t bleRespHandle = 0;
static uint16_t bleConnHandle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t bleMtu = 23;
static uint8_t bleOwnAddrType = 0;
static String bleCmdBuf;

static void bleStartAdvertising();

static String bleReject(const char *error, const char *reason, int index = -1)
{
    String j = "{\"ok\":false,\"error\":\"";
    j += error;
    j += "\"";
    if (reason)
    {
        j += ",\"reason\":\"";
        j += reason;
        j += "\"";
    }
    if (index >= 0)
    {
        j += ",\"index\":";
        j += index;
    }
    j += "}";
    return j;
}

// ---- send: intentionally unavailable ---------------------------------------
//
// BLE used to parse arbitrary frames and transmit directly through the driver.
// That bypassed the roadmap's required TxIntent/policy/scheduler boundary.
// Keep the protocol verb as an explicit fail-closed response so old clients
// receive a clear error, but retain no frame parser or physical-send path.

static String bleHandleSend(JsonObjectConst /*args*/)
{
    return bleReject("unsupported", "raw BLE CAN send removed; TxIntent required");
}

// ---- read-only status -------------------------------------------------------

// Live runtime figures: what the car and the link are actually doing. Separate
// from `status` so the frequently polled reply stays small.
static String bleBuildStatsJson()
{
    uint32_t now = millis();
    DashApGateSnapshot gate = dashApGateSnapshot();
    String j = "{\"ok\":true";
    j += ",\"uptimeS\":";
    j += (unsigned long)(now / 1000);
    j += ",\"canFrames\":";
    j += (unsigned long)RuntimeDiagnostics::canFrames.load(std::memory_order_relaxed);
    j += ",\"canAgeMs\":";
    j += (unsigned long)RuntimeDiagnostics::canAgeMs(now);
    j += ",\"txOk\":";
    j += (unsigned long)RuntimeDiagnostics::txOk.load(std::memory_order_relaxed);
    j += ",\"txFail\":";
    j += (unsigned long)RuntimeDiagnostics::txFail.load(std::memory_order_relaxed);
    j += ",\"freeHeap\":";
    j += (unsigned long)esp_get_free_heap_size();
    // The vehicle state the injection gates key off, so the app can explain a
    // "gated" rejection instead of just reporting it.
    j += ",\"gateEnabled\":";
    j += gate.enabled ? "true" : "false";
    j += ",\"gateAllowed\":";
    j += gate.allowed ? "true" : "false";
    j += ",\"apActive\":";
    j += gate.apActive ? "true" : "false";
    j += ",\"parked\":";
    j += gate.parked ? "true" : "false";
    j += ",\"summoning\":";
    j += gate.summoning ? "true" : "false";
    j += ",\"gateReason\":\"";
    j += gate.reason;
    j += "\"}";
    return j;
}

// Transport-neutral command dispatch. Seed of the shared command core that a
// future HTTP /api endpoint can reuse.
static String bleDispatchCommand(JsonObjectConst root)
{
    const char *cmd = root["cmd"] | "";
    if (strcmp(cmd, "status") == 0)
        return dashBuildBleStatusJson();
    if (strcmp(cmd, "ping") == 0)
        return String("{\"ok\":true,\"pong\":true}");
    if (strcmp(cmd, "send") == 0)
        return bleHandleSend(root["args"]);
    if (strcmp(cmd, "stats") == 0)
        return bleBuildStatsJson();
    if (strcmp(cmd, "snapshot") == 0)
        return dashBuildBleMaintenanceSnapshotJson();
    if (strcmp(cmd, "config") == 0)
    {
        // No args means read; any args mean apply them. Same call the dashboard
        // makes, so validation cannot differ between the two transports.
        JsonObjectConst args = root["args"];
        if (args.isNull() || args.size() == 0)
        {
            String j = "{\"ok\":true,\"config\":";
            j += ctrlBuildConfigJson();
            j += "}";
            return j;
        }
        return bleReject("unauthorized", "device-owner authorization required");
    }
    if (strcmp(cmd, "inject") == 0)
        return bleReject("unauthorized", "device-owner CAN-arm permission required");
    if (strcmp(cmd, "wifi_mode") == 0)
        return bleReject("unauthorized", "device-owner admin permission required");
    return String("{\"ok\":false,\"error\":\"unknown cmd\"}");
}

// The most recent reply, fetched by the client via paged READs of the Response
// characteristic. A single GATT attribute value is capped at 512 bytes by the
// spec, and replies (status, config, backups) can be larger, so each read
// returns an 8-byte little-endian header (uint32 total, uint32 offset) followed
// by up to kBleReadPage bytes of the reply. The client reads repeatedly,
// advancing until it has `total` bytes. Reliable at any MTU.
static const uint32_t kBleReadPage = 400;
static String bleRespBuffer = "{}";
static uint32_t bleRespCursor = 0;

static void bleStoreResponse(const String &payload)
{
    bleRespBuffer = payload;
    bleRespCursor = 0;
}

static int bleCmdWriteCb(uint16_t /*conn_handle*/, uint16_t /*attr_handle*/,
                         struct ble_gatt_access_ctxt *ctxt, void * /*arg*/)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
        return BLE_ATT_ERR_UNLIKELY;

    char buf[256];
    uint16_t outLen = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &outLen) != 0)
        return BLE_ATT_ERR_UNLIKELY;
    buf[outLen] = '\0';
    bleCmdBuf += buf;

    // Guard against unbounded growth from a peer that never sends a newline.
    if (bleCmdBuf.length() > 4096)
        bleCmdBuf = "";

    int nl;
    while ((nl = bleCmdBuf.indexOf('\n')) >= 0)
    {
        String cmd = bleCmdBuf.substring(0, nl);
        bleCmdBuf = bleCmdBuf.substring(nl + 1);
        if (!cmd.length())
            continue;
        // Parse once. Passing c_str() (const) avoids ArduinoJson's in-place
        // parse mutating the String. Trailing '\r' is tolerated as whitespace.
        JsonDocument doc;
        if (deserializeJson(doc, cmd.c_str()))
        {
            bleStoreResponse(String("{\"ok\":false,\"error\":\"bad json\"}"));
            continue;
        }
        // {"cmd":"next"} pages through the current reply without replacing it.
        if (strcmp(doc["cmd"] | "", "next") == 0)
        {
            uint32_t total = (uint32_t)bleRespBuffer.length();
            bleRespCursor = (bleRespCursor + kBleReadPage < total)
                                ? bleRespCursor + kBleReadPage
                                : total;
            continue;
        }
        bleStoreResponse(bleDispatchCommand(doc.as<JsonObjectConst>()));
    }
    return 0;
}

static int bleRespAccessCb(uint16_t /*conn*/, uint16_t /*attr*/,
                           struct ble_gatt_access_ctxt *ctxt, void * /*arg*/)
{
    // Reads return the next page of the last command's reply (header + slice).
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        uint32_t total = (uint32_t)bleRespBuffer.length();
        uint32_t off = bleRespCursor > total ? total : bleRespCursor;
        uint32_t remaining = total - off;
        uint32_t n = remaining < kBleReadPage ? remaining : kBleReadPage;
        uint8_t hdr[8] = {
            (uint8_t)total, (uint8_t)(total >> 8), (uint8_t)(total >> 16), (uint8_t)(total >> 24),
            (uint8_t)off, (uint8_t)(off >> 8), (uint8_t)(off >> 16), (uint8_t)(off >> 24)};
        if (os_mbuf_append(ctxt->om, hdr, sizeof(hdr)) != 0)
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        if (n > 0 &&
            os_mbuf_append(ctxt->om, bleRespBuffer.c_str() + off, n) != 0)
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        // Do NOT advance the cursor here: a single logical read is assembled by
        // the client from several ATT Read Blob requests, each of which calls
        // this callback, so the page must stay stable. The client advances to
        // the next page with a {"cmd":"next"} write.
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Partial designated initializers leave later members zero, which is intended;
// silence -Werror=missing-field-initializers (NimBLE's own examples do the same).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const struct ble_gatt_svc_def bleGattSvcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &bleSvcUuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &bleCmdUuid.u,
                .access_cb = bleCmdWriteCb,
                // Encrypted + authenticated: only a peer that paired with the
                // passkey (bonded) can send commands. Locks out third parties.
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC |
                         BLE_GATT_CHR_F_WRITE_AUTHEN,
            },
            {
                .uuid = &bleRespUuid.u,
                .access_cb = bleRespAccessCb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY |
                         BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN,
                .val_handle = &bleRespHandle,
            },
            {0},
        },
    },
    {0},
};
#pragma GCC diagnostic pop

static int bleGapEvent(struct ble_gap_event *event, void * /*arg*/)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            bleConnHandle = event->connect.conn_handle;
            bleMtu = 23;
            // The central initiates the ATT MTU exchange; we answer with the
            // preferred MTU set in bleServiceSetup. bleMtu is updated in the
            // BLE_GAP_EVENT_MTU event below.
        }
        else
        {
            bleStartAdvertising();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        bleConnHandle = BLE_HS_CONN_HANDLE_NONE;
        bleCmdBuf = "";
        bleStartAdvertising();
        return 0;
    case BLE_GAP_EVENT_MTU:
        bleMtu = event->mtu.value;
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        bleStartAdvertising();
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        // The central asks us to "display" the passkey; inject the dashboard PIN
        // so pairing is MITM-protected without a real screen on the device.
        if (event->passkey.params.action == BLE_SM_IOACT_DISP)
        {
            struct ble_sm_io pk;
            memset(&pk, 0, sizeof(pk));
            pk.action = BLE_SM_IOACT_DISP;
            pk.passkey = dashBlePasskey;
            ble_sm_inject_io(event->passkey.conn_handle, &pk);
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(kBleTag, "encryption change; status=%d", event->enc_change.status);
        return 0;
    default:
        return 0;
    }
}

static void bleStartAdvertising()
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)kBleDeviceName;
    fields.name_len = strlen(kBleDeviceName);
    fields.name_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0)
        return;

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(bleOwnAddrType, NULL, BLE_HS_FOREVER, &adv_params,
                      bleGapEvent, NULL);
}

static void bleOnSync()
{
    if (ble_hs_util_ensure_addr(0) != 0)
        return;
    if (ble_hs_id_infer_auto(0, &bleOwnAddrType) != 0)
        return;
    bleStartAdvertising();
}

static void bleOnReset(int reason)
{
    ESP_LOGW(kBleTag, "BLE stack reset; reason=%d", reason);
}

static void bleHostTask(void * /*param*/)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void bleServiceSetup()
{
    if (nimble_port_init() != ESP_OK)
    {
        ESP_LOGE(kBleTag, "nimble_port_init failed");
        return;
    }
    ble_hs_cfg.sync_cb = bleOnSync;
    ble_hs_cfg.reset_cb = bleOnReset;

    // Security: LE Secure Connections with a passkey (MITM) and bonding. The
    // device "displays" the passkey (we inject the dashboard-configured PIN);
    // the user types it into the app. Bonds persist in NVS.
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_att_set_preferred_mtu(247);
    ble_svc_gap_init();
    ble_svc_gatt_init();

    if (ble_gatts_count_cfg(bleGattSvcs) != 0 ||
        ble_gatts_add_svcs(bleGattSvcs) != 0)
    {
        ESP_LOGE(kBleTag, "GATT registration failed");
        return;
    }
    ble_svc_gap_device_name_set(kBleDeviceName);
    nimble_port_freertos_init(bleHostTask);
}

#endif // BLE_APP && !NATIVE_BUILD
