#pragma once

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)

#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#else
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Update.h>
#endif
#include <cerrno>
#include <exception>
#include <new>
#include <memory>
#include <cstdlib>
#include <soc/soc_caps.h>
#ifdef ESP_PLATFORM
#include <esp_mac.h>
#include <freertos/semphr.h>
#include <fcntl.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <psa/crypto.h>
#include <unistd.h>
#endif
#ifndef ESP_PLATFORM
#include <Preferences.h>
#include <SPIFFS.h>
#endif
#include "handlers.h"
#include "bounded_text_writer.h"
#include "can_helpers.h"
#include "plugin_engine.h"
#include "chassis/decoder_registry.h"
#include "chassis/layout_recommendation.h"
#include "chassis/telemetry_state.h"
#include "chassis/event_recorder.h"
#include "diagnostics/can_anomaly_tracker.h"
#include "experimental/assist_controls.h"
#include "tx/tx_scheduler.h"
#if defined(DRIVER_ESP32_EXT_MCP2515)
#include "drivers/esp32_mcp2515_driver.h"
#endif
#include "web/mcp2515_dashboard_ui.h"
#ifdef ESP_PLATFORM
#include <esp_random.h>
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
#ifndef FIRMWARE_GIT_REV
#define FIRMWARE_GIT_REV "unknown"
#endif
#ifndef FIRMWARE_BUILD_ENV
#define FIRMWARE_BUILD_ENV "unknown"
#endif
#ifndef FIRMWARE_ARTIFACT
#define FIRMWARE_ARTIFACT "firmware.bin"
#endif

#ifndef DASH_SSID
#error "Define -DDASH_SSID in build_flags (e.g. -DDASH_SSID=\\\"ADUnlock-1234\\\")"
#endif
#ifndef DASH_PASS
#error "Define -DDASH_PASS in build_flags (min 8 chars)"
#endif
#ifndef DASH_OTA_PASS
#error "Define -DDASH_OTA_PASS in build_flags"
#endif
#ifndef DASH_OTA_USER
#error "Define -DDASH_OTA_USER in build_flags"
#endif

static_assert(sizeof(DASH_SSID) > 1 && sizeof(DASH_SSID) <= 33, "DASH_SSID must be 1-32 bytes");
static_assert(sizeof(DASH_PASS) >= 9 && sizeof(DASH_PASS) <= 64, "DASH_PASS must be 8-63 bytes");

#ifndef DASH_DEFAULT_HW
#define DASH_DEFAULT_HW 1
#endif

#if defined(DASH_INJECTION_ON_BOOT)
static constexpr bool kDashInjectionDefaultEnabled = true;
#else
static constexpr bool kDashInjectionDefaultEnabled = false;
#endif

// Boot straight into dev/test mode (simulated CAN) when no stored preference
// exists — for bench units flashed without a bus attached.
#if defined(DASH_DEV_MODE_ON_BOOT)
static constexpr bool kDashDevModeDefault = true;
#else
static constexpr bool kDashDevModeDefault = false;
#endif

#if defined(INJECTION_AFTER_AP) || defined(DASH_INJECTION_AFTER_AP)
static constexpr bool kDashApGateDefaultEnabled = true;
#else
static constexpr bool kDashApGateDefaultEnabled = false;
#endif

#if defined(DRIVER_TWAI)
#ifndef TWAI_TX_PIN
#define TWAI_TX_PIN GPIO_NUM_5
#endif
#ifndef TWAI_RX_PIN
#define TWAI_RX_PIN GPIO_NUM_4
#endif
#endif

#if DASH_DEFAULT_HW < 0 || DASH_DEFAULT_HW > 2
#error "DASH_DEFAULT_HW must be 0 (LEGACY), 1 (HW3), or 2 (HW4)"
#endif

#ifndef DASH_ALLOW_CAN_GPIO_6_11
#define DASH_ALLOW_CAN_GPIO_6_11 0
#endif

static bool dashCanPinReserved(int pin)
{
#if DASH_ALLOW_CAN_GPIO_6_11
    (void)pin;
    return false;
#else
    return pin >= 6 && pin <= 11;
#endif
}

#define PREFS_NS "ADunlock"
static constexpr uint8_t kDashUnsetU8 = 0xFF;
static constexpr uint16_t kRecorderSettingInjection = 1;
static constexpr uint16_t kRecorderSettingHardware = 2;
static constexpr uint16_t kRecorderSettingCan = 3;
static constexpr uint16_t kRecorderSettingSpeedAuto = 4;
static constexpr uint16_t kRecorderSettingSpeedProfile = 5;
static constexpr uint16_t kRecorderSettingApGate = 6;
static constexpr uint16_t kRecorderSettingSummonOnly = 7;
static constexpr uint16_t kRecorderSettingNagMode = 8;
static constexpr uint16_t kRecorderSettingReplayCount = 9;
static constexpr uint16_t kRecorderSettingOffsetSlew = 10;
static constexpr uint16_t kRecorderSettingOffsetSlewRate = 11;

static Preferences prefs;

class DashDataGuard
{
public:
    DashDataGuard()
    {
#ifdef ESP_PLATFORM
        initialize();
        if (mutex_)
            locked_ = xSemaphoreTakeRecursive(mutex_, portMAX_DELAY) == pdTRUE;
#endif
    }

    ~DashDataGuard()
    {
#ifdef ESP_PLATFORM
        if (locked_)
            xSemaphoreGiveRecursive(mutex_);
#endif
    }

    static bool initialize()
    {
#ifdef ESP_PLATFORM
        if (!mutex_)
            mutex_ = xSemaphoreCreateRecursiveMutex();
        return mutex_ != nullptr;
#else
        return true;
#endif
    }

private:
#ifdef ESP_PLATFORM
    inline static SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
#endif
};

class DashWifiGuard
{
public:
    DashWifiGuard()
    {
#ifdef ESP_PLATFORM
        initialize();
        if (mutex_)
            locked_ = xSemaphoreTakeRecursive(mutex_, portMAX_DELAY) == pdTRUE;
#endif
    }

    ~DashWifiGuard()
    {
#ifdef ESP_PLATFORM
        if (locked_)
            xSemaphoreGiveRecursive(mutex_);
#endif
    }

    static bool initialize()
    {
#ifdef ESP_PLATFORM
        if (!mutex_)
            mutex_ = xSemaphoreCreateRecursiveMutex();
        return mutex_ != nullptr;
#else
        return true;
#endif
    }

private:
#ifdef ESP_PLATFORM
    inline static SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
#endif
};

class DashPrefsGuard
{
public:
    DashPrefsGuard()
    {
#ifdef ESP_PLATFORM
        initialize();
        if (mutex_)
            locked_ = xSemaphoreTakeRecursive(mutex_, portMAX_DELAY) == pdTRUE;
#endif
    }

    ~DashPrefsGuard()
    {
#ifdef ESP_PLATFORM
        if (locked_)
            xSemaphoreGiveRecursive(mutex_);
#endif
    }

    static bool initialize()
    {
#ifdef ESP_PLATFORM
        if (!mutex_)
            mutex_ = xSemaphoreCreateRecursiveMutex();
        return mutex_ != nullptr;
#else
        return true;
#endif
    }

private:
#ifdef ESP_PLATFORM
    inline static SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
#endif
};

class DashHandlerRef
{
public:
    DashHandlerRef &operator=(CarManagerBase *handler)
    {
        value_ = handler;
        return *this;
    }
    operator CarManagerBase *() const { return value_; }
    CarManagerBase *operator->() const { return value_; }

private:
    Shared<CarManagerBase *> value_{nullptr};
};

static DashHandlerRef dashHandler;
static CanDriver *dashDriver = nullptr;
// Read-only telemetry collected from explicitly classified CAN frames. This
// state never feeds a transmit path; it is only exposed through /status.
static Chassis::TelemetryState dashTelemetry{Chassis::DasLayout::Unknown, 1500};
static Chassis::LayoutRecommendation::Tracker dashLayoutTracker;
static CanAnomaly::Tracker dashAnomalyTracker;
static Chassis::EventRecorder dashRecorder;
static TxControl::Scheduler dashTxScheduler;
static TxControl::Scheduler dashCanBTxScheduler;
static uint32_t dashTxSessionNonce = 0;

static bool dashAnomalyBlocksTx()
{
    DashDataGuard guard;
    dashAnomalyTracker.tick(millis());
    return dashAnomalyTracker.summary().policyBlock;
}

class DashRecorderConfigUpdate
{
public:
    explicit DashRecorderConfigUpdate(Chassis::EventRecorder &recorder) : recorder_(recorder)
    {
        DashDataGuard guard;
        recorder_.beginConfigurationUpdate();
    }

    ~DashRecorderConfigUpdate()
    {
        DashDataGuard guard;
        recorder_.endConfigurationUpdate(millis());
    }

    DashRecorderConfigUpdate(const DashRecorderConfigUpdate &) = delete;
    DashRecorderConfigUpdate &operator=(const DashRecorderConfigUpdate &) = delete;

private:
    Chassis::EventRecorder &recorder_;
};
static bool dashIncidentPersisted = false;
static bool dashIncidentPersistFailure = false;
static uint32_t dashPersistedGeneration = 0;
static uint32_t dashPersistFailureGeneration = 0;
static bool dashPersistInProgress = false;
static char dashIncidentPath[64] = "/incident.jsonl";
static uint32_t dashIncidentEvictions = 0;
static uint16_t dashIncidentPendingCount = 0;
static uint16_t dashIncidentAcknowledgedCount = 0;
static size_t dashIncidentStorageTotalBytes = 0;
static size_t dashIncidentStorageFreeBytes = 0;
static bool dashIncidentStoragePressure = false;
static char dashIncidentLastError[48] = {};
static constexpr size_t kDashIncidentSafetyFreeBytes = 256 * 1024;
static constexpr size_t kDashIncidentListLimit = 16;
static constexpr size_t kDashIncidentAckTombstoneLimit = 128;
static bool dashCanSeen = false;
static uint32_t dashCanLossGeneration = 0;
static bool dashVehicleCanSeen = false;
static uint32_t dashLastVehicleFrameMs = 0;
#if defined(DRIVER_ESP32_EXT_MCP2515)
static ESP32_MCP2515Driver *dashMcpDriver = nullptr;
#endif

static unsigned long lastFrameMs = 0;
static bool canOnline = false;

static Shared<uint8_t> hwMode{DASH_DEFAULT_HW};
// 0=Auto, 1=legacy 0x399 byte0, 2=standard HW4 0x39B byte1,
// 3=explicit Highland 0x39B byte0. Never infer this from observed frames.
static Shared<uint8_t> dashDasLayoutOverride{0};
static Shared<bool> canActive{kDashInjectionDefaultEnabled};
// Dev/test mode (simulated CAN traffic) — see app.h appDevSim / appDevModeActive.
static Shared<bool> dashDevMode{kDashDevModeDefault};
// WiFi and BLE cannot run together reliably on ESP32 classic (shared radio), so
// the device boots into exactly one mode. false = WiFi dashboard (default),
// true = BLE-only (WiFi off). Persisted in NVS; switched with a reboot.
static Shared<bool> dashBleMode{false};
// 6-digit BLE pairing passkey (LE Secure Connections, MITM). Shown in the
// dashboard; entered in the app to pair. Generated once, persisted in NVS.
static Shared<uint32_t> dashBlePasskey{0};
static Shared<bool> apInjectionGate{kDashApGateDefaultEnabled};
static Shared<bool> summonOnlyInjection{false};
static Shared<uint8_t> dashNagMode{static_cast<uint8_t>(NagMode::Disabled)};
static Shared<bool> dashUlcStalkConfirm{false};
static Shared<bool> dashUlcOffHighway{false};
static Shared<uint8_t> dashUlcSpeedConfig{ExperimentalAssist::kPreserveSetting};
static Shared<uint8_t> dashUlcBlindSpotConfig{ExperimentalAssist::kPreserveSetting};
static Shared<bool> dashSummonEuUnlock{false};
static Shared<bool> dashHandsOn247DryRunEnabled{false};
static ExperimentalAssist::HandsOn247DryRun dashHandsOn247DryRun;
static Shared<bool> dashSpeedProfileAuto{true};
static Shared<uint8_t> dashManualSpeedProfile{1};
static NagHandler dashNagHandler;

static constexpr uint8_t kHw3SlewRateMin = 1;
static constexpr uint8_t kHw3SlewRateMax = 25;
static constexpr uint8_t kHw3SlewRateDefault = 5;
static Shared<bool> hw3OffsetSlew{false};
static Shared<uint8_t> hw3SlewRate{kHw3SlewRateDefault};
static Shared<uint8_t> hw3OffsetTargetRaw{0};
static Shared<uint8_t> hw3OffsetLastRaw{0};
static Shared<uint32_t> hw3OffsetLastSentMs{0};
static Shared<uint32_t> hw3OffsetSlewCount{0};

#ifdef RGB_BRIGHTNESS
static constexpr uint8_t kDashLedBrightnessDefault = RGB_BRIGHTNESS;
#else
static constexpr uint8_t kDashLedBrightnessDefault = 32;
#endif
static Shared<uint8_t> dashLedBrightness{kDashLedBrightnessDefault};

// WiFi AP (hotspot) — overridable at runtime
static char apSSID[33] = "";
static char apPass[65] = "";
static bool apHidden = false; // when true, SSID is not broadcast (hidden AP)
static constexpr size_t kDashMaxSsidLen = 32;
static constexpr size_t kDashMinApPassLen = 8;
static constexpr size_t kDashMaxPassLen = 63;
static constexpr int kDashApChannel = 1;
static constexpr int kDashApMaxConn = 4;

#ifdef ESP_PLATFORM
// Small, unauthenticated local discovery only. The response contains no
// credentials, addresses, CAN data, or control capability. The actual
// read-only HTTP endpoints remain authenticated.
static constexpr uint16_t kDashDiscoveryPort = 36991;
static constexpr char kDashDiscoveryRequest[] = "T2CAN_DISCOVER_V1";
static constexpr char kDashDiscoveryResponse[] =
    "{\"schema\":\"t2can-discovery-v1\",\"service\":\"EVCANTool\",\"port\":80,\"readOnly\":true}\n";
static int dashDiscoverySocket = -1;
#endif

// WiFi STA (client) mode for internet access
static char staSSID[33] = "";
static char staPass[65] = "";
static bool staConnected = false;
static bool staConnectAttemptActive = false;
static bool staStaticIP = false;

// Multi-SSID storage
static constexpr uint8_t kDashMaxWifiNetworks = 4;
struct DashWifiNetwork
{
    char ssid[33];
    char pass[65];
    bool useStatic;
    char ip[16];
    char gw[16];
    char mask[16];
    char dns[16];
};
static DashWifiNetwork wifiNetworks[kDashMaxWifiNetworks] = {};
static uint8_t wifiNetworkCount = 0;
static int8_t wifiActiveSlot = -1;    // slot currently selected for STA attempt
static int8_t wifiNextRotateSlot = 0; // next slot to try when rotating
static Shared<bool> updateBetaChannel{false};
static Shared<bool> autoUpdateEnabled{false};
static Shared<bool> autoUpdateDone{false};            // one-shot per boot
static Shared<unsigned long> autoUpdateEligibleAt{0}; // millis() at which auto-check may fire
static unsigned long staConnectStartedAt = 0;
static unsigned long staRetryAt = 0;
static constexpr unsigned long kDashStaBootDelayMs = 5000;
static constexpr unsigned long kDashStaConnectTimeoutMs = 25000;
static constexpr unsigned long kDashStaRetryMs = 120000;
static IPAddress staIP(0, 0, 0, 0);
static IPAddress staGW(0, 0, 0, 0);
static IPAddress staMask(255, 255, 255, 0);
static IPAddress staDNS(0, 0, 0, 0);

static bool dashStaConnectedSnapshot()
{
    DashWifiGuard guard;
    return staConnected;
}

// Multi-SSID NVS helpers (key form: w0s, w0p, w0t, w0i, w0g, w0m, w0d)
static String dashWifiKey(uint8_t slot, const char *sub)
{
    String k = "w";
    k += slot;
    k += sub;
    return k;
}
static void dashClearWifiNetwork(DashWifiNetwork &n)
{
    n.ssid[0] = 0;
    n.pass[0] = 0;
    n.useStatic = false;
    n.ip[0] = 0;
    n.gw[0] = 0;
    n.mask[0] = 0;
    n.dns[0] = 0;
}
static void dashRotateAndConnect();
static void dashSwapHandler(uint8_t mode);
static void dashApplyFilters();
static void dashReapplyFiltersWithPlugins();
static uint8_t dashCollectMergedFilterIds(uint32_t *ids, uint8_t maxIds);
static void dashApplyRuntimeState(bool refreshLed = true);
static void dashRecordEffectiveConfiguration();
static void dashRecordEffectiveConfigurationAt(uint32_t now, uint8_t replayCount);
static void mcpDashOnActiveSpeedProfile(uint8_t profile);
static void dashRestorePluginStates();
static void dashClearLegacyOptionPrefs();
static void dashSchedulePluginStateSave(unsigned long delayMs = 750);
static void dashFlushPluginStatesIfDue();

static Shared<bool> pluginStatesDirty{false};
static Shared<unsigned long> pluginStatesFlushAt{0};

enum DashWriteProbeState : uint8_t
{
    kDashWriteProbeIdle = 0,
    kDashWriteProbePending = 1,
    kDashWriteProbeMatch = 2,
    kDashWriteProbeDifferent = 3,
    kDashWriteProbeFailed = 4,
};

struct DashWriteProbe
{
    bool active = false;
    bool hasRx = false;
    uint8_t state = kDashWriteProbeIdle;
    uint32_t id = 0;
    int8_t mux = -1;
    uint8_t txDlc = 0;
    uint8_t rxDlc = 0;
    uint8_t txData[8] = {};
    uint8_t rxData[8] = {};
    unsigned long txMs = 0;
    unsigned long rxMs = 0;
};
static DashWriteProbe dashWriteProbe;

static int8_t dashFrameMux(const CanFrame &frame)
{
    if ((frame.id == 1006 || frame.id == 1021) && frame.dlc > 0)
        return static_cast<int8_t>(readMuxID(frame));
    return -1;
}

static void dashResetWriteProbe()
{
    dashWriteProbe = {};
    dashWriteProbe.mux = -1;
    dashWriteProbe.state = kDashWriteProbeIdle;
}

static bool dashWriteProbeMatches(const CanFrame &frame)
{
    if (!dashWriteProbe.active || dashWriteProbe.id != frame.id)
        return false;

    int8_t mux = dashFrameMux(frame);
    if (dashWriteProbe.mux < 0)
        return mux < 0;
    return mux == dashWriteProbe.mux;
}

static void dashLog(const String &s)
{
    Serial.println(s);
}

// Public hooks
static void mcpDashOnFrame(const CanFrame &f)
{
    DashDataGuard guard;
    unsigned long now = millis();
    const bool telemetryAccepted = dashTelemetry.observe(f, now);
    dashLayoutTracker.observe(f, now);
    dashAnomalyTracker.observe(f, now);
    dashRecorder.observe(f, now);
    const Chassis::TelemetrySnapshot telemetry = dashTelemetry.snapshot(now);
    if (telemetryAccepted && (f.id == 0x399 || f.id == 0x39B))
        dashRecorder.noteAp(telemetry.apState, now);
    if (telemetry.dasSeen)
        dashRecorder.recordNag(dashNagMode, now);
    if (telemetryAccepted && f.id == 0x108 && telemetry.torqueSeen && f.dlc >= 2)
    {
        const uint16_t torqueRaw =
            (static_cast<uint16_t>(f.data[1] & 0x1F) << 8) | f.data[0];
        dashRecorder.recordTorque(torqueRaw, now);
    }
    lastFrameMs = now;
    canOnline = true;
    dashCanSeen = true;
    if (telemetryAccepted && Chassis::isChassisBus(f.bus))
    {
        dashVehicleCanSeen = true;
        dashLastVehicleFrameMs = now;
    }
    if (dashWriteProbe.active && dashWriteProbe.state != kDashWriteProbeFailed && dashWriteProbeMatches(f))
    {
        dashWriteProbe.hasRx = true;
        dashWriteProbe.rxMs = now;
        dashWriteProbe.rxDlc = (f.dlc <= 8) ? f.dlc : 8;
        memset(dashWriteProbe.rxData, 0, sizeof(dashWriteProbe.rxData));
        memcpy(dashWriteProbe.rxData, f.data, dashWriteProbe.rxDlc);
        bool same = dashWriteProbe.txDlc == dashWriteProbe.rxDlc &&
                    memcmp(dashWriteProbe.txData, dashWriteProbe.rxData, dashWriteProbe.txDlc) == 0;
        dashWriteProbe.state = same ? kDashWriteProbeMatch : kDashWriteProbeDifferent;
    }
}

static void mcpDashOnTxFrame(const CanFrame &frame, bool ok)
{
    DashDataGuard guard;
    const uint32_t now = millis();
    if (!dashDriver || !dashDriver->reportsPhysicalTxAttempts())
        dashRecorder.observeTx(frame, ok, now);
    int8_t mux = dashFrameMux(frame);
    dashWriteProbe.active = true;
    dashWriteProbe.hasRx = false;
    dashWriteProbe.state = ok ? kDashWriteProbePending : kDashWriteProbeFailed;
    dashWriteProbe.id = frame.id;
    dashWriteProbe.mux = mux;
    dashWriteProbe.txMs = now;
    dashWriteProbe.rxMs = 0;
    dashWriteProbe.txDlc = (frame.dlc <= 8) ? frame.dlc : 8;
    dashWriteProbe.rxDlc = 0;
    memset(dashWriteProbe.txData, 0, sizeof(dashWriteProbe.txData));
    memset(dashWriteProbe.rxData, 0, sizeof(dashWriteProbe.rxData));
    memcpy(dashWriteProbe.txData, frame.data, dashWriteProbe.txDlc);
}

static void mcpDashOnTxAttemptFrame(const CanFrame &frame, bool ok, bool attempted)
{
    DashDataGuard guard;
    dashRecorder.observeTx(frame, ok, millis(), attempted);
}

static void mcpDashOnInjectionDecision(bool allowed, const char *reason)
{
    uint16_t code = 0;
    if (reason && strcmp(reason, "startup_or_can_stale") == 0) code = 1;
    else if (reason && strcmp(reason, "handler_unavailable") == 0) code = 2;
    else if (reason && strcmp(reason, "summon_policy_blocked") == 0) code = 3;
    else if (reason && strcmp(reason, "can_disabled") == 0) code = 4;
    else if (reason && strcmp(reason, "ap_gate_blocked") == 0) code = 5;
    else if (reason && strcmp(reason, "nag_disabled") == 0) code = 6;
    DashDataGuard guard;
    dashRecorder.recordInjectionDecision(allowed, code, millis());
}

static void mcpDashRecordCanHealth()
{
    if (!dashDriver)
        return;
    bool readyA = false, readyB = false, readyAny = false;
    uint32_t errorsA = 0, errorsB = 0, errorsAny = 0;
    const bool hasA = dashDriver->physicalHealth(CAN_BUS_CAN_A, readyA, errorsA);
    const bool hasB = dashDriver->physicalHealth(CAN_BUS_CAN_B, readyB, errorsB);
    if (!hasA && !hasB && dashDriver->physicalHealth(CAN_BUS_ANY, readyAny, errorsAny))
    {
        DashDataGuard guard;
        dashRecorder.recordCanHealth(2, readyAny, errorsAny, millis());
        return;
    }
    DashDataGuard guard;
    const uint32_t recordNow = millis();
    if (hasA)
        dashRecorder.recordCanHealth(0, readyA, errorsA, recordNow);
    if (hasB)
        dashRecorder.recordCanHealth(1, readyB, errorsB, recordNow);
}

// JSON escape for log strings
static String jsonEscape(const String &s)
{
    String out;
    out.reserve(s.length() + 8);
    for (unsigned int i = 0; i < s.length(); i++)
    {
        char c = s.charAt(i);
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c < 0x20)
            out += ' ';
        else
            out += c;
    }
    return out;
}

static bool dashParseLong(const String &text, long &value)
{
    if (text.length() == 0)
        return false;
    size_t i = text[0] == '-' ? 1 : 0;
    if (i == text.length())
        return false;
    for (; i < text.length(); i++)
    {
        if (text[i] < '0' || text[i] > '9')
            return false;
    }
    char *end = nullptr;
    errno = 0;
    value = strtol(text.c_str(), &end, 10);
    return errno == 0 && end && *end == '\0';
}

static bool dashParseBool(const String &text, bool &value)
{
    if (text == "0")
    {
        value = false;
        return true;
    }
    if (text == "1")
    {
        value = true;
        return true;
    }
    return false;
}

static bool dashCheckADEnabled()
{
    if (!canActive && appDashboardDecisionObserver)
        appDashboardDecisionObserver(false, "can_disabled");
    return canActive;
}

static constexpr unsigned long kDashApInjectionStableDelayMs = 1000;
static unsigned long dashApStableStartedMs = 0;
static bool dashApStableTracking = false;
static bool dashLastApForLog = false;

static unsigned long dashTrackApStableMs(bool ap, unsigned long now)
{
    DashDataGuard guard;
    if (ap && !dashLastApForLog)
    {
        dashApStableStartedMs = now;
        dashApStableTracking = true;
        dashLog("[APGATE] AP rising edge");
    }
    else if (!ap && dashLastApForLog)
    {
        dashApStableStartedMs = 0;
        dashApStableTracking = false;
        dashLog("[APGATE] AP falling edge");
    }
    dashLastApForLog = ap;
    return (ap && dashApStableTracking) ? now - dashApStableStartedMs : 0;
}

static bool dashApInjectionAllowed()
{
    if (!apInjectionGate)
        return true;
    if (!dashHandler)
        return false;

    unsigned long now = millis();
    bool ap = dashHandler->APActive;
    bool parked = dashHandler->Parked;
    bool summoning = dashHandler->Summoning;
    unsigned long apStableMs = dashTrackApStableMs(ap, now);
    return injectionGateOpenWithStableAp(ap, parked, summoning, apStableMs,
                                         kDashApInjectionStableDelayMs);
}

struct DashApGateSnapshot
{
    bool enabled = false;
    bool allowed = false;
    bool apActive = false;
    bool parked = false;
    bool summoning = false;
    unsigned long stableMs = 0;
    const char *reason = "disabled";
};

static DashApGateSnapshot dashApGateSnapshot()
{
    DashApGateSnapshot snapshot;
    snapshot.enabled = apInjectionGate;
    if (!snapshot.enabled)
    {
        snapshot.allowed = true;
        return snapshot;
    }
    if (!dashHandler)
    {
        snapshot.reason = "handler unavailable";
        return snapshot;
    }

    {
        AppHandlerGuard guard;
        snapshot.apActive = dashHandler->APActive;
        snapshot.parked = dashHandler->Parked;
        snapshot.summoning = dashHandler->Summoning;
    }
    {
        DashDataGuard guard;
        if (snapshot.apActive && dashApStableTracking)
            snapshot.stableMs = millis() - dashApStableStartedMs;
    }
    snapshot.allowed = injectionGateOpenWithStableAp(
        snapshot.apActive, snapshot.parked, snapshot.summoning, snapshot.stableMs,
        kDashApInjectionStableDelayMs);
    if (snapshot.parked)
        snapshot.reason = "parked";
    else if (snapshot.summoning)
        snapshot.reason = "summoning";
    else if (!snapshot.apActive)
        snapshot.reason = "AP inactive";
    else if (!snapshot.allowed)
        snapshot.reason = "AP stabilizing";
    else
        snapshot.reason = "AP stable";
    return snapshot;
}

static SummonInjectionDecision dashSummonOnlyInjectionDecision()
{
    if (!summonOnlyInjection)
        return {true, SummonInjectionState::Disabled};
    if (!dashHandler)
        return {false, SummonInjectionState::MissingDiState};
    AppHandlerGuard guard;
    return dashHandler->summonOnlyInjectionDecisionAt(millis());
}

static bool dashSummonOnlyInjectionAllowed()
{
    return dashSummonOnlyInjectionDecision().allowed;
}

// Final physical-TX implementation of the dashboard AP-gate toggle. OFF keeps
// the pre-2026.14 continuous RX-echo behaviour. ON requires fresh CAN state to
// prove Park+stationary, stable AP, or an active Summon session. Keeping this at
// the driver boundary prevents direct handler/plugin sends from bypassing it.
static bool dashActivityTxAllowed()
{
    if (dashDevMode)
        return true; // simulated loopback only; no physical CAN attempt
    if (!apInjectionGate)
        return true;
    if (!dashHandler)
        return false;

    const uint32_t now = millis();
    Chassis::TelemetrySnapshot telemetry;
    {
        DashDataGuard guard;
        telemetry = dashTelemetry.snapshot(now);
    }
    const bool apActive = telemetry.dasSeen && isDASAutopilotActive(telemetry.apState);
    const unsigned long apStableMs = dashTrackApStableMs(apActive, now);
    const bool stableAp = apActive && apStableMs >= kDashApInjectionStableDelayMs;
    const SummonInjectionDecision stateDecision = evaluateSummonInjectionPolicy(
        dashHandler->summonOnlyInjectionSnapshot(), now);
    return stableAp || stateDecision.allowed;
}

static void dashRefreshSummonOnlyPolicy()
{
    static bool initialized = false;
    static SummonInjectionState previousState = SummonInjectionState::Disabled;
    SummonInjectionDecision decision = dashSummonOnlyInjectionDecision();
    if (initialized && decision.state == previousState)
        return;

    initialized = true;
    previousState = decision.state;
    if (summonOnlyInjection && !decision.allowed)
    {
        pluginResetPeriodicEmit();
        if (dashDriver)
            dashDriver->clearPendingTransmit();
    }
    dashLog(String("[SUMMON] ") + summonInjectionStateName(decision.state));
}

static bool dashInjectionActive()
{
    const bool canEnabled = canActive;
    if (!canEnabled)
    {
        if (appDashboardDecisionObserver)
            appDashboardDecisionObserver(false, "can_disabled");
        return false;
    }

    const bool canReady = appInjectionReady();
    if (!canReady)
    {
        if (appDashboardDecisionObserver)
            appDashboardDecisionObserver(false, "startup_or_can_stale");
        return false;
    }

    const bool apAllowed = dashApInjectionAllowed();
    if (!apAllowed)
    {
        if (appDashboardDecisionObserver)
            appDashboardDecisionObserver(false, "ap_gate_blocked");
        return false;
    }

    const bool summonAllowed = dashSummonOnlyInjectionAllowed();
    if (!summonAllowed)
    {
        if (appDashboardDecisionObserver)
            appDashboardDecisionObserver(false, "summon_policy_blocked");
        return false;
    }

    if (appDashboardDecisionObserver)
        appDashboardDecisionObserver(true, "allowed");
    return true;
}

static TxControl::PolicyContext dashTxPolicyContextForBus(uint8_t physicalBus)
{
    TxControl::PolicyContext context;
    context.nowMs = millis();
    context.sessionNonce = dashTxSessionNonce;
    context.session = dashInjectionActive() ? TxControl::Session::Active
                                            : TxControl::Session::Observe;
    context.masterEnabled = static_cast<bool>(canActive);
    context.startupFresh = appInjectionReady();
    context.vehicleFresh = context.startupFresh;
    context.otaInhibit = static_cast<bool>(appMaintenanceTxInhibit);
    context.controlAuthorized = true; // Built-in feature, not a remote principal.
    context.controlConnected = true;
    context.parked = dashHandler && static_cast<bool>(dashHandler->Parked);
    Chassis::TelemetrySnapshot telemetry;
    {
        DashDataGuard guard;
        telemetry = dashTelemetry.snapshot(context.nowMs);
    }
    context.stationary = telemetry.speedSeen && telemetry.speedKph >= -0.1f &&
                         telemetry.speedKph <= 0.1f;
    bool summonEligible = false;
    if (dashHandler)
    {
        const SummonInjectionDecision summonDecision = evaluateSummonInjectionPolicy(
            dashHandler->summonOnlyInjectionSnapshot(), context.nowMs);
        summonEligible = summonDecision.allowed;
    }
    const bool freshApActive = telemetry.dasSeen && isDASAutopilotActive(telemetry.apState);
    // AP gate OFF is the explicit legacy/pre-2026.14 continuous mode. When ON,
    // individual assist intents inherit the same validated activity conditions.
    context.summonEligible = !static_cast<bool>(apInjectionGate) || summonEligible;
    context.assistActivity = !static_cast<bool>(apInjectionGate) ||
                             freshApActive || summonEligible;
    bool ready = false;
    uint32_t errors = 0;
    context.busHealthy = dashDriver &&
                         dashDriver->physicalHealth(physicalBus, ready, errors) && ready;
    return context;
}

static TxControl::PolicyContext dashTxPolicyContext(void *)
{
    return dashTxPolicyContextForBus(CAN_BUS_CAN_A);
}

static TxControl::PolicyContext dashCanBTxPolicyContext(void *)
{
    return dashTxPolicyContextForBus(CAN_BUS_CAN_B);
}

static bool dashSubmitNagTx(const CanFrame &frame, CanDriver &driver, uint32_t)
{
    if (&driver != dashDriver)
        return false;
    TxControl::TxIntent intent = dashTxScheduler.prepare(
        TxControl::Source::BuiltIn, 0x370, frame,
        CAN_BUS_PARTY, CAN_BUS_CAN_A, TxControl::Session::Active, 100);
    intent.cadenceMs = 5;
    intent.maxBurst = 200;
    intent.cooldownMs = 1000;
    intent.counter = TxControl::CounterStrategy::IncrementObserved;
    intent.checksum = TxControl::ChecksumStrategy::VerifiedGenerator;
    intent.requirements = static_cast<uint16_t>(TxControl::RequireStartupFresh |
                                                TxControl::RequireVehicleFresh);
    const TxControl::Submission result = dashTxScheduler.submit(intent);
    if (!result.policy.allowed && appDashboardDecisionObserver)
        appDashboardDecisionObserver(false, TxControl::reasonName(result.policy.reason));
    return result.driverSucceeded;
}

static ExperimentalAssist::Config dashExperimentalAssistConfig()
{
    ExperimentalAssist::Config config;
    config.ulcStalkConfirm = static_cast<bool>(dashUlcStalkConfirm);
    config.ulcOffHighway = static_cast<bool>(dashUlcOffHighway);
    config.ulcSpeedConfig = static_cast<uint8_t>(dashUlcSpeedConfig);
    config.ulcBlindSpotConfig = static_cast<uint8_t>(dashUlcBlindSpotConfig);
    config.summonEuUnlock = static_cast<bool>(dashSummonEuUnlock);
    return config;
}

static bool dashSubmitAssistTx(const CanFrame &frame, CanDriver &driver,
                               uint16_t requirements)
{
    if (&driver != dashDriver)
        return false;
    TxControl::TxIntent intent = dashCanBTxScheduler.prepare(
        TxControl::Source::BuiltIn, static_cast<uint16_t>(frame.id), frame,
        CAN_BUS_CH, CAN_BUS_CAN_B, TxControl::Session::Active, 100);
    intent.cadenceMs = 20;
    intent.maxBurst = 50;
    intent.cooldownMs = 1000;
    intent.counter = TxControl::CounterStrategy::PreserveObserved;
    intent.checksum = TxControl::ChecksumStrategy::PreserveObserved;
    intent.requirements = requirements;
    const TxControl::Submission result = dashCanBTxScheduler.submit(intent);
    if (!result.policy.allowed && appDashboardDecisionObserver)
        appDashboardDecisionObserver(false, TxControl::reasonName(result.policy.reason));
    return result.driverSucceeded;
}

static void mcpDashOnExperimentalFrame(const CanFrame &observed, CanDriver &driver)
{
    if (observed.id == ExperimentalAssist::kHandsOnCandidateId ||
        observed.id == ExperimentalAssist::kHandsOnContextId)
    {
        DashDataGuard guard;
        dashHandsOn247DryRun.observe(observed, millis());
    }

    const ExperimentalAssist::Config config = dashExperimentalAssistConfig();
    CanFrame modified;
    if ((hwMode == 1 || hwMode == 2) &&
        ExperimentalAssist::prepareUlcEcho(config, observed, modified))
    {
        dashSubmitAssistTx(modified, driver,
                           static_cast<uint16_t>(TxControl::RequireStartupFresh |
                                                 TxControl::RequireVehicleFresh |
                                                 TxControl::RequireAssistActivity));
        return;
    }

    if (hwMode == 2 &&
        ExperimentalAssist::prepareSummonEuHw4Echo(config, observed, modified))
    {
        dashSubmitAssistTx(modified, driver,
                           static_cast<uint16_t>(TxControl::RequireStartupFresh |
                                                 TxControl::RequireVehicleFresh |
                                                 TxControl::RequireSummonEligible));
    }
}

static bool dashCheckNagDisabled()
{
    if (appDashboardDecisionObserver)
        appDashboardDecisionObserver(false, "nag_disabled");
    return false;
}

static bool dashStaSsidLooksCorrupt(const String &ssid)
{
    if (ssid.indexOf("\"ssid\"") >= 0 || ssid.indexOf("{\"") >= 0 ||
        ssid.indexOf("\",\"") >= 0)
        return true;
    for (size_t i = 0; i < ssid.length(); i++)
    {
        unsigned char c = static_cast<unsigned char>(ssid[i]);
        if (c < 0x20 || c == 0x7F)
            return true;
    }
    return false;
}

static Chassis::DasLayout dashEffectiveDasLayout()
{
    switch ((uint8_t)dashDasLayoutOverride)
    {
    case 1:
        return Chassis::DasLayout::LegacyHw3;
    case 2:
        return Chassis::DasLayout::StandardHw4;
    case 3:
        return Chassis::DasLayout::HighlandHw4Byte0;
    default:
        return (uint8_t)hwMode == 2 ? Chassis::DasLayout::StandardHw4
                                    : Chassis::DasLayout::LegacyHw3;
    }
}

static const char *dashDasLayoutName(uint8_t value)
{
    switch (value)
    {
    case 1:
        return "legacy_0x399_byte0";
    case 2:
        return "standard_hw4_0x39b_byte1";
    case 3:
        return "highland_0x39b_byte0";
    default:
        return "auto";
    }
}

static uint8_t dashClampDasLayoutOverride(int value)
{
    return value >= 0 && value <= 3 ? static_cast<uint8_t>(value) : 0;
}

static uint8_t dashClampHw3SlewRate(int rate)
{
    if (rate < kHw3SlewRateMin)
        return kHw3SlewRateMin;
    if (rate > kHw3SlewRateMax)
        return kHw3SlewRateMax;
    return static_cast<uint8_t>(rate);
}

static uint8_t dashLoadHw3SlewRate(uint8_t rate)
{
    if (rate < kHw3SlewRateMin || rate > kHw3SlewRateMax)
        return kHw3SlewRateDefault;
    return rate;
}

static uint8_t dashClampSpeedProfileForHw(uint8_t hw, int profile)
{
    int maxProfile = hw == 2 ? 4 : 2;
    if (profile < 0)
        return 0;
    if (profile > maxProfile)
        return static_cast<uint8_t>(maxProfile);
    return static_cast<uint8_t>(profile);
}

static void dashApplySpeedProfileState()
{
    if (!dashHandler)
        return;
    dashHandler->speedProfileAuto = (bool)dashSpeedProfileAuto;
    if (!(bool)dashSpeedProfileAuto)
        dashHandler->speedProfile = dashClampSpeedProfileForHw(hwMode, dashManualSpeedProfile);
}

static bool dashReadHw3OffsetRaw(const CanFrame &frame, uint8_t &raw)
{
    if ((hwMode != 1 && hwMode != 2) || frame.id != 1021 || frame.dlc < 2 || readMuxID(frame) != 2)
        return false;

    raw = static_cast<uint8_t>(((frame.data[1] & 0x3F) << 2) | ((frame.data[0] >> 6) & 0x03));
    return true;
}

static void dashWriteHw3OffsetRaw(CanFrame &frame, uint8_t raw)
{
    frame.data[0] = static_cast<uint8_t>((frame.data[0] & ~0xC0) | ((raw & 0x03) << 6));
    frame.data[1] = static_cast<uint8_t>((frame.data[1] & ~0x3F) | (raw >> 2));
}

static bool dashApplyHw3OffsetSlew(CanFrame &modified, const CanFrame & /*original*/)
{
    uint8_t activeRaw = 0;
    if (!dashReadHw3OffsetRaw(modified, activeRaw))
        return false;

    hw3OffsetTargetRaw = activeRaw;
    uint8_t shapedRaw = activeRaw;
    uint32_t now = millis();

    if (hw3OffsetSlew)
    {
        uint8_t last = hw3OffsetLastRaw;
        if (activeRaw < last && hw3OffsetLastSentMs != 0)
        {
            uint32_t rateRawPerSec = static_cast<uint32_t>(dashLoadHw3SlewRate(hw3SlewRate)) * 4;
            uint32_t dt = now - hw3OffsetLastSentMs;
            uint32_t maxDrop = (rateRawPerSec * dt + 500) / 1000;
            uint8_t floorRaw = last > maxDrop ? static_cast<uint8_t>(last - maxDrop) : 0;
            if (activeRaw < floorRaw)
            {
                shapedRaw = floorRaw;
                hw3OffsetSlewCount++;
            }
        }
    }

    hw3OffsetLastRaw = shapedRaw;
    hw3OffsetLastSentMs = now;
    if (shapedRaw == activeRaw)
        return false;

    dashWriteHw3OffsetRaw(modified, shapedRaw);
    return true;
}

static void dashInvalidateTxSession()
{
    uint32_t next = esp_random();
    if (next == 0 || next == dashTxSessionNonce)
    {
        next = dashTxSessionNonce + 1;
        if (next == 0)
            next = 1;
    }
    dashTxSessionNonce = next;
    dashTxScheduler.cancelAll();
    dashCanBTxScheduler.cancelAll();
}

static void dashApplyRuntimeState(bool refreshLed)
{
    // Runtime callers hold AppHandlerGuard; setup invokes this before CAN tasks
    // start. Rotate the nonce as well as clearing admission history so an intent
    // prepared under the previous configuration cannot be submitted afterward.
    dashInvalidateTxSession();
    bypassTlsscRequirementRuntime = false;
    emergencyVehicleDetectionRuntime = false;
    isaSpeedChimeSuppressRuntime = false;
    enhancedAutopilotRuntime = false;
    if (!nagModeAllowedForHardware(dashNagMode, hwMode))
        dashNagMode = static_cast<uint8_t>(NagMode::Disabled);
    nagKillerRuntime = canActive && dashNagMode != static_cast<uint8_t>(NagMode::Disabled);
    dashNagHandler.setMode(dashNagMode);
    dashNagHandler.setHardwareMode(hwMode);
    const Chassis::DasLayout dasLayout = dashEffectiveDasLayout();
    dashTelemetry.setLayout(dasLayout);
    if (dashHandler)
        dashHandler->setDasLayout(dasLayout);
    summonOnlyInjectionRuntime = static_cast<bool>(summonOnlyInjection);
    if (hwMode != 2)
        dashSummonEuUnlock = false;
    dashHandsOn247DryRun.setEnabled(static_cast<bool>(dashHandsOn247DryRunEnabled));

    if (dashHandler)
    {
        dashHandler->checkAD = dashCheckADEnabled;
        dashHandler->checkNag = dashCheckNagDisabled;
        dashApplySpeedProfileState();
        if (!canActive)
        {
            dashHandler->ADEnabled = false;
            dashHandler->APActive = false;
        }
    }

#if defined(DASH_RGB_STATUS_LED)
    if (refreshLed)
        appRefreshStatusLed();
#else
    (void)refreshLed;
#endif
}

// Store config
static bool dashSavePrefs()
{
    DashPrefsGuard guard;
    if (!prefs.begin(PREFS_NS, false))
        return false;
    prefs.putUChar("hw", hwMode);
    prefs.putUChar("hw_def", DASH_DEFAULT_HW);
    prefs.putUChar("das_layout", dashDasLayoutOverride);
    prefs.putBool("can", canActive);
    prefs.putBool("ap_gate", apInjectionGate);
    prefs.putBool("sum_only", summonOnlyInjection);
    prefs.putUChar("nag_mode", dashNagMode);
    prefs.putBool("ulc_stalk", dashUlcStalkConfirm);
    prefs.putBool("ulc_offhwy", dashUlcOffHighway);
    prefs.putUChar("ulc_speed", dashUlcSpeedConfig);
    prefs.putUChar("ulc_blind", dashUlcBlindSpotConfig);
    prefs.putBool("sum_eu", dashSummonEuUnlock);
    prefs.putBool("h247_dry", dashHandsOn247DryRunEnabled);
    prefs.putBool("sp_auto", dashSpeedProfileAuto);
    prefs.putUChar("sp_sel", dashManualSpeedProfile);
    prefs.putUChar("plg_rep", pluginGetReplayCount());
    prefs.putBool("h3_slw", hw3OffsetSlew);
    prefs.putUChar("h3_srt", hw3SlewRate);
    prefs.putUChar("led_b", dashLedBrightness);
    return prefs.end();
}

static bool dashSetCanActive(bool active, const char *reason = nullptr)
{
    AppHandlerGuard appGuard;
    const bool previous = canActive;
    const bool changed = previous != active;
    {
        PluginLockGuard pluginGuard;
        DashDataGuard dataGuard;
        DashRecorderConfigUpdate recorderUpdate(dashRecorder);
        const uint32_t now = millis();
        canActive = active;
        dashApplyRuntimeState(false);
        dashRecorder.recordSetting(kRecorderSettingInjection, active ? 1u : 0u, now);
        dashRecorder.recordSetting(kRecorderSettingCan, active ? 1u : 0u, now);
        dashRecordEffectiveConfigurationAt(now, pluginGetReplayCountLocked());
    }
#if defined(DASH_RGB_STATUS_LED)
    appRefreshStatusLed();
#endif
    const bool saved = dashSavePrefs();
    if (!saved)
        dashLog("[ERR] Failed to persist dashboard settings");
    if (changed)
    {
        String msg = String("[CFG] Injection ") + (active ? "ON" : "OFF");
        if (reason && *reason)
            msg += String(" via ") + reason;
        dashLog(msg);
    }
    return saved;
}

[[maybe_unused]] static void dashToggleCanActive(const char *reason = nullptr)
{
    dashSetCanActive(!canActive, reason);
}

// Dev/test mode: apply the runtime state (frame source + TX loopback). appDev*
// live in app.h, which includes this header after declaring them.
static void dashApplyDevMode(bool on)
{
    appDevModeActive = on;
    if (dashDriver)
        dashDriver->setSimLoopback(on);
    if (on)
        appDevSimReset();
}

static void dashSetDevMode(bool on)
{
    dashDevMode = on;
    dashApplyDevMode(on);
    prefs.begin(PREFS_NS, false);
    prefs.putBool("dev_mode", on);
    prefs.end();
    dashLog(String("[DEV] Test mode ") + (on ? "ON (simulated CAN traffic)" : "OFF"));
}

// Persist the WiFi/BLE mode and reboot into it. WiFi and BLE share the radio on
// ESP32 classic and cannot run together reliably, so the choice is per-boot.
// True while a switch to BLE mode is still unproven. The safety net that falls
// back to WiFi only applies during this window: it exists to rescue a user who
// switched to BLE and then could not connect, not to police steady-state
// operation. Without it being one-shot, a device that lives in BLE mode drops
// back to WiFi on every power cycle where nobody connects within the timeout --
// which for something plugged into a car is most of them.
static Shared<bool> dashBleProbation{false};

static void dashSetBleMode(bool on)
{
    prefs.begin(PREFS_NS, false);
    prefs.putBool("ble_mode", on);
    // Arm the safety net on the way in; leaving BLE mode disarms it.
    prefs.putBool("ble_prob", on);
    prefs.end();
    dashBleProbation = on;
    dashLog(String("[MODE] Switching to ") + (on ? "BLE" : "WiFi") + " mode; rebooting...");
    delay(300);
    ESP.restart();
}

/** Called on the first BLE connection: the mode works, so stop second-guessing it. */
static void dashClearBleProbation()
{
    if (!(bool)dashBleProbation)
        return;
    dashBleProbation = false;
    prefs.begin(PREFS_NS, false);
    prefs.putBool("ble_prob", false);
    prefs.end();
    dashLog("[MODE] BLE mode confirmed by a client; WiFi fallback disarmed");
}

static String ctrlBuildConfigJson();
static String dashBuildBleStatusJson();

// Read-only maintenance snapshot shared with the phone bridge. It deliberately
// contains health and receive-side telemetry only; it does not arm injection,
// change configuration, or expose raw credentials.
static String dashBuildBleMaintenanceSnapshotJson()
{
    const uint32_t now = millis();
    Chassis::TelemetrySnapshot telemetry{};
    {
        DashDataGuard guard;
        telemetry = dashTelemetry.snapshot(now);
    }

    char driverJson[768] = "{\"type\":\"unavailable\",\"stateCode\":0}";
    if (dashDriver)
        dashDriver->diagnosticsJson(driverJson, sizeof(driverJson));

    String j = "{\"ok\":true,\"schema\":\"t2can-maintenance-snapshot-v1\",\"readOnly\":true";
    j += ",\"firmware\":\"";
    j += jsonEscape(String(FIRMWARE_VERSION));
    j += "\",\"uptimeS\":";
    j += (unsigned long)(now / 1000);
    j += ",\"status\":";
    j += dashBuildBleStatusJson();
    j += ",\"runtime\":{\"canFrames\":";
    j += (unsigned long)RuntimeDiagnostics::canFrames.load(std::memory_order_relaxed);
    j += ",\"canAgeMs\":";
    j += (unsigned long)RuntimeDiagnostics::canAgeMs(now);
    j += ",\"txOk\":";
    j += (unsigned long)RuntimeDiagnostics::txOk.load(std::memory_order_relaxed);
    j += ",\"txFail\":";
    j += (unsigned long)RuntimeDiagnostics::txFail.load(std::memory_order_relaxed);
    j += ",\"freeHeap\":";
    j += (unsigned long)esp_get_free_heap_size();
    j += "}";
    j += ",\"telemetry\":{\"acceptedFrames\":";
    j += (unsigned long)telemetry.acceptedFrames;
    j += ",\"lastObservedMs\":";
    j += (unsigned long)telemetry.lastObservedMs;
    j += ",\"speed\":{\"seen\":";
    j += telemetry.speedSeen ? "true" : "false";
    j += ",\"kph\":";
    j += telemetry.speedKph;
    j += ",\"ageMs\":";
    j += telemetry.speedSeen ? (unsigned long)(now - telemetry.speedMs) : 0UL;
    j += "},\"gear\":{\"seen\":";
    j += telemetry.gearSeen ? "true" : "false";
    j += ",\"value\":";
    j += (unsigned int)telemetry.gear;
    j += ",\"autonomy\":";
    j += telemetry.autonomyActive ? "true" : "false";
    j += ",\"ageMs\":";
    j += telemetry.gearSeen ? (unsigned long)(now - telemetry.gearMs) : 0UL;
    j += "},\"steering\":{\"seen\":";
    j += telemetry.steeringSeen ? "true" : "false";
    j += ",\"deg\":";
    j += telemetry.steeringAngleDeg;
    j += ",\"ageMs\":";
    j += telemetry.steeringSeen ? (unsigned long)(now - telemetry.steeringMs) : 0UL;
    j += "},\"brake\":{\"seen\":";
    j += telemetry.brakeSeen ? "true" : "false";
    j += ",\"applied\":";
    j += telemetry.brakeApplied ? "true" : "false";
    j += ",\"ageMs\":";
    j += telemetry.brakeSeen ? (unsigned long)(now - telemetry.brakeMs) : 0UL;
    j += "},\"das\":{\"seen\":";
    j += telemetry.dasSeen ? "true" : "false";
    j += ",\"ap\":";
    j += (unsigned int)telemetry.apState;
    j += ",\"handsOn\":";
    j += (unsigned int)telemetry.handsOn;
    j += ",\"ageMs\":";
    j += telemetry.dasSeen ? (unsigned long)(now - telemetry.dasMs) : 0UL;
    j += "}}";
    j += ",\"driver\":";
    j += driverJson;
    j += ",\"config\":";
    j += ctrlBuildConfigJson();
    j += "}";
    return j;
}

// Compact status for the BLE app (independent of the detailed HTTP /status so
// dev's handler is untouched).
static String dashBuildBleStatusJson()
{
    String j = "{\"ok\":true";
    j += ",\"dev\":";
    j += (bool)dashDevMode ? "true" : "false";
    j += ",\"ble\":";
    j += (bool)dashBleMode ? "true" : "false";
    j += ",\"hw\":";
    j += (int)(uint8_t)hwMode;
    j += ",\"inject\":";
    j += (bool)canActive ? "true" : "false";
    j += ",\"injectActive\":";
    j += dashInjectionActive() ? "true" : "false";
#ifdef ESP_PLATFORM
    j += ",\"uptimeS\":";
    j += (unsigned long)(millis() / 1000);
#endif
    j += "}";
    return j;
}

static bool dashApPasswordLengthValid(size_t len)
{
    return len >= kDashMinApPassLen && len <= kDashMaxPassLen;
}

static bool dashApConfigValid(const char *ssid, const char *pass)
{
    size_t ssidLen = strlen(ssid);
    size_t passLen = strlen(pass);
    return ssidLen > 0 && ssidLen <= kDashMaxSsidLen &&
           !dashStaSsidLooksCorrupt(String(ssid)) && dashApPasswordLengthValid(passLen);
}

static void dashUseDefaultApConfig()
{
    strlcpy(apSSID, DASH_SSID, sizeof(apSSID));
    strlcpy(apPass, DASH_PASS, sizeof(apPass));
    apHidden = false;
}

static bool dashStaConfigLengthValid(const String &ssid, const String &pass)
{
    return ssid.length() <= kDashMaxSsidLen && pass.length() <= kDashMaxPassLen;
}

static bool dashParseCanonicalIpv4(const char *text, uint32_t &value)
{
    if (!text || *text == '\0')
        return false;
    const char *cursor = text;
    value = 0;
    for (uint8_t part = 0; part < 4; part++)
    {
        if (*cursor < '0' || *cursor > '9')
            return false;
        const char *start = cursor;
        unsigned int octet = 0;
        while (*cursor >= '0' && *cursor <= '9')
        {
            octet = octet * 10 + static_cast<unsigned int>(*cursor - '0');
            if (octet > 255)
                return false;
            cursor++;
        }
        if (cursor - start > 1 && *start == '0')
            return false;
        value = (value << 8) | octet;
        if (part < 3)
        {
            if (*cursor++ != '.')
                return false;
        }
        else if (*cursor != '\0')
            return false;
    }
    return true;
}

static bool dashStaticIpConfigValid(const char *ip, const char *gateway, const char *mask, const char *dns)
{
    uint32_t ipValue = 0, gatewayValue = 0, maskValue = 0, dnsValue = 0;
    if (!dashParseCanonicalIpv4(ip, ipValue) || !dashParseCanonicalIpv4(gateway, gatewayValue) ||
        !dashParseCanonicalIpv4(mask, maskValue) || !dashParseCanonicalIpv4(dns, dnsValue) ||
        ipValue == 0 || gatewayValue == 0 || maskValue == 0 || dnsValue == 0)
        return false;
    uint32_t invertedMask = ~maskValue;
    if ((invertedMask & (invertedMask + 1)) != 0)
        return false;
    return (ipValue & maskValue) == (gatewayValue & maskValue);
}

static void dashClearLegacyOptionPrefs()
{
    static const char *const keys[] = {
        "fAD",
        "f_AD",
        "f_nag",
        "f_sum",
        "f_isa",
        "f_evd",
        "f_h4o",
        "sp",
        "sp_lock",
    };

    bool removed = false;
    for (const char *key : keys)
    {
        if (!prefs.isKey(key))
            continue;
        prefs.remove(key);
        removed = true;
    }

    if (removed)
        dashLog("[BOOT] Cleared legacy dashboard prefs from NVS");
}

static void dashLoadPrefs()
{
    DashPrefsGuard guard;
    prefs.begin(PREFS_NS, false);
    dashClearLegacyOptionPrefs();
    bool hasStoredHw = prefs.isKey("hw");
    uint8_t storedHw = prefs.getUChar("hw", DASH_DEFAULT_HW);
    uint8_t storedDefaultHw = prefs.getUChar("hw_def", kDashUnsetU8);
    bool migratedHw = false;

    hwMode = storedHw <= 2 ? storedHw : DASH_DEFAULT_HW;
    if (!hasStoredHw || storedHw > 2)
        migratedHw = true;

    // If the stored selection only mirrors the old firmware default, follow the
    // new build default after reflashing instead of staying pinned to stale NVS.
    if (storedDefaultHw <= 2 && storedDefaultHw != DASH_DEFAULT_HW && hwMode == storedDefaultHw)
    {
        hwMode = DASH_DEFAULT_HW;
        migratedHw = true;
    }

    if (migratedHw)
        prefs.putUChar("hw", hwMode);
    if (storedDefaultHw != DASH_DEFAULT_HW)
        prefs.putUChar("hw_def", DASH_DEFAULT_HW);
    uint8_t storedDasLayout = prefs.getUChar("das_layout", 0);
    dashDasLayoutOverride = dashClampDasLayoutOverride(storedDasLayout);
    if (storedDasLayout != dashDasLayoutOverride)
        prefs.putUChar("das_layout", dashDasLayoutOverride);
    canActive = prefs.getBool("can", kDashInjectionDefaultEnabled);
    dashDevMode = prefs.getBool("dev_mode", kDashDevModeDefault);
    dashBleMode = prefs.getBool("ble_mode", false);
    // Default true for a device already in BLE mode from before this flag
    // existed: one unproven boot is the safe assumption, and the first
    // connection clears it for good.
    dashBleProbation = dashBleMode && prefs.getBool("ble_prob", true);
    {
        String pinStr = prefs.getString("ble_pin", "");
        uint32_t pin = pinStr.length() ? (uint32_t)atoi(pinStr.c_str()) : 0;
        if (pin < 100000 || pin > 999999)
        {
            pin = 100000 + (esp_random() % 900000);
            prefs.putString("ble_pin", String((unsigned long)pin));
            dashLog("[BLE] Generated new pairing passkey");
        }
        dashBlePasskey = pin;
    }
    apInjectionGate = prefs.getBool("ap_gate", kDashApGateDefaultEnabled);
    summonOnlyInjection = prefs.getBool("sum_only", false);
    dashNagMode = clampNagMode(prefs.getUChar("nag_mode", static_cast<uint8_t>(NagMode::Disabled)));
    dashUlcStalkConfirm = prefs.getBool("ulc_stalk", false);
    dashUlcOffHighway = prefs.getBool("ulc_offhwy", false);
    {
        const uint8_t stored = prefs.getUChar("ulc_speed", ExperimentalAssist::kPreserveSetting);
        dashUlcSpeedConfig = stored <= 3 ? stored : ExperimentalAssist::kPreserveSetting;
    }
    {
        const uint8_t stored = prefs.getUChar("ulc_blind", ExperimentalAssist::kPreserveSetting);
        dashUlcBlindSpotConfig = stored <= 2 ? stored : ExperimentalAssist::kPreserveSetting;
    }
    dashSummonEuUnlock = prefs.getBool("sum_eu", false);
    dashHandsOn247DryRunEnabled = prefs.getBool("h247_dry", false);
    dashSpeedProfileAuto = prefs.getBool("sp_auto", true);
    dashManualSpeedProfile = dashClampSpeedProfileForHw(hwMode, prefs.getUChar("sp_sel", 1));
    pluginSetReplayCount(prefs.getUChar("plg_rep", PLUGIN_REPLAY_COUNT));
    hw3OffsetSlew = prefs.getBool("h3_slw", false);
    hw3SlewRate = dashLoadHw3SlewRate(prefs.getUChar("h3_srt", kHw3SlewRateDefault));
    dashLedBrightness = prefs.getUChar("led_b", kDashLedBrightnessDefault);
    dashApplyRuntimeState();
    dashRecordEffectiveConfiguration();
    // Load WiFi AP overrides (hotspot name/password)
    String apSsidPref = prefs.isKey("ap_ssid") ? prefs.getString("ap_ssid", "") : "";
    String apPassPref = prefs.isKey("ap_pass") ? prefs.getString("ap_pass", "") : "";
    bool hasApOverride = apSsidPref.length() > 0 || apPassPref.length() > 0 || prefs.isKey("ap_hidden");
    bool invalidApOverride = apSsidPref.length() > kDashMaxSsidLen ||
                             (apPassPref.length() > 0 && !dashApPasswordLengthValid(apPassPref.length()));
    if (apSsidPref.length() > 0)
        strlcpy(apSSID, apSsidPref.c_str(), sizeof(apSSID));
    else
        strlcpy(apSSID, DASH_SSID, sizeof(apSSID));
    if (apPassPref.length() > 0)
        strlcpy(apPass, apPassPref.c_str(), sizeof(apPass));
    else
        strlcpy(apPass, DASH_PASS, sizeof(apPass));
    apHidden = prefs.getBool("ap_hidden", false);
    if (invalidApOverride || !dashApConfigValid(apSSID, apPass))
    {
        if (hasApOverride)
        {
            prefs.remove("ap_ssid");
            prefs.remove("ap_pass");
            prefs.remove("ap_hidden");
            dashLog("[WIFI] Invalid saved AP config ignored");
        }
        dashUseDefaultApConfig();
    }

    // Load WiFi STA networks (multi-SSID slot array)
    wifiNetworkCount = 0;
    for (uint8_t i = 0; i < kDashMaxWifiNetworks; i++)
        dashClearWifiNetwork(wifiNetworks[i]);

    uint8_t storedCount = prefs.getUChar("wn_cnt", 0);
    if (storedCount > kDashMaxWifiNetworks)
        storedCount = kDashMaxWifiNetworks;

    for (uint8_t i = 0; i < storedCount; i++)
    {
        DashWifiNetwork &n = wifiNetworks[wifiNetworkCount];
        String s = prefs.getString(dashWifiKey(i, "s").c_str(), "");
        String p = prefs.getString(dashWifiKey(i, "p").c_str(), "");
        if (!dashStaConfigLengthValid(s, p) || dashStaSsidLooksCorrupt(s) || s.length() == 0)
            continue;
        strlcpy(n.ssid, s.c_str(), sizeof(n.ssid));
        strlcpy(n.pass, p.c_str(), sizeof(n.pass));
        n.useStatic = prefs.getBool(dashWifiKey(i, "t").c_str(), false);
        if (n.useStatic)
        {
            String ip = prefs.getString(dashWifiKey(i, "i").c_str(), "0.0.0.0");
            String gw = prefs.getString(dashWifiKey(i, "g").c_str(), "0.0.0.0");
            String mk = prefs.getString(dashWifiKey(i, "m").c_str(), "255.255.255.0");
            String dn = prefs.getString(dashWifiKey(i, "d").c_str(), "0.0.0.0");
            if (!dashStaticIpConfigValid(ip.c_str(), gw.c_str(), mk.c_str(), dn.c_str()))
            {
                dashClearWifiNetwork(n);
                continue;
            }
            strlcpy(n.ip, ip.c_str(), sizeof(n.ip));
            strlcpy(n.gw, gw.c_str(), sizeof(n.gw));
            strlcpy(n.mask, mk.c_str(), sizeof(n.mask));
            strlcpy(n.dns, dn.c_str(), sizeof(n.dns));
        }
        wifiNetworkCount++;
    }

    // One-shot migration from legacy single-SSID keys
    if (wifiNetworkCount == 0 && prefs.isKey("wifi_ssid"))
    {
        String s = prefs.getString("wifi_ssid", "");
        String p = prefs.getString("wifi_pass", "");
        if (dashStaConfigLengthValid(s, p) && !dashStaSsidLooksCorrupt(s) && s.length() > 0)
        {
            DashWifiNetwork &n = wifiNetworks[0];
            strlcpy(n.ssid, s.c_str(), sizeof(n.ssid));
            strlcpy(n.pass, p.c_str(), sizeof(n.pass));
            n.useStatic = prefs.getBool("wifi_static", false);
            if (n.useStatic)
            {
                String ip = prefs.getString("wifi_ip", "0.0.0.0");
                String gateway = prefs.getString("wifi_gw", "0.0.0.0");
                String mask = prefs.getString("wifi_mask", "255.255.255.0");
                String dns = prefs.getString("wifi_dns", "0.0.0.0");
                if (!dashStaticIpConfigValid(ip.c_str(), gateway.c_str(), mask.c_str(), dns.c_str()))
                    n.useStatic = false;
                else
                {
                    strlcpy(n.ip, ip.c_str(), sizeof(n.ip));
                    strlcpy(n.gw, gateway.c_str(), sizeof(n.gw));
                    strlcpy(n.mask, mask.c_str(), sizeof(n.mask));
                    strlcpy(n.dns, dns.c_str(), sizeof(n.dns));
                }
            }
            wifiNetworkCount = 1;
            prefs.putUChar("wn_cnt", 1);
            prefs.putString(dashWifiKey(0, "s").c_str(), s);
            prefs.putString(dashWifiKey(0, "p").c_str(), p);
            prefs.putBool(dashWifiKey(0, "t").c_str(), n.useStatic);
            if (n.useStatic)
            {
                prefs.putString(dashWifiKey(0, "i").c_str(), String(n.ip));
                prefs.putString(dashWifiKey(0, "g").c_str(), String(n.gw));
                prefs.putString(dashWifiKey(0, "m").c_str(), String(n.mask));
                prefs.putString(dashWifiKey(0, "d").c_str(), String(n.dns));
            }
            dashLog("[WIFI] Migrated legacy STA config to slot 0");
        }
        prefs.remove("wifi_ssid");
        prefs.remove("wifi_pass");
        prefs.remove("wifi_static");
        prefs.remove("wifi_ip");
        prefs.remove("wifi_gw");
        prefs.remove("wifi_mask");
        prefs.remove("wifi_dns");
    }

    // Seed staSSID/staPass with first slot for compat with existing connect path
    if (wifiNetworkCount > 0)
    {
        const DashWifiNetwork &n = wifiNetworks[0];
        strlcpy(staSSID, n.ssid, sizeof(staSSID));
        strlcpy(staPass, n.pass, sizeof(staPass));
        staStaticIP = n.useStatic;
        if (n.useStatic)
        {
            staIP.fromString(n.ip);
            staGW.fromString(n.gw);
            staMask.fromString(n.mask);
            staDNS.fromString(n.dns);
        }
        wifiActiveSlot = 0;
        wifiNextRotateSlot = wifiNetworkCount > 1 ? 1 : 0;
    }
    else
    {
        staSSID[0] = 0;
        staPass[0] = 0;
        staStaticIP = false;
        wifiActiveSlot = -1;
        wifiNextRotateSlot = 0;
    }

    updateBetaChannel = prefs.getBool("update_beta", false);
    autoUpdateEnabled = prefs.getBool("auto_upd", false);
    prefs.end();

    if (migratedHw)
        dashLog("[BOOT] HW default synced to " + String(hwMode == 0 ? "LEGACY" : hwMode == 1 ? "HW3"
                                                                                             : "HW4"));
    dashLog("[BOOT] Prefs loaded HW=" + String(hwMode));
    dashLog("[BOOT] canActive=" + String(canActive ? "YES" : "NO"));
    dashLog("[BOOT] pluginReplay=" + String(pluginGetReplayCount()));
}

static void dashRecordEffectiveConfigurationAt(uint32_t now, uint8_t replayCount)
{
    // Caller holds DashDataGuard; the timestamp is the effective-commit time.
    const bool effectiveCanActive = canActive;
    dashRecorder.recordSetting(kRecorderSettingInjection, effectiveCanActive ? 1u : 0u, now);
    dashRecorder.recordSetting(kRecorderSettingHardware, hwMode, now);
    dashRecorder.recordSetting(kRecorderSettingCan, effectiveCanActive ? 1u : 0u, now);
    dashRecorder.recordSetting(kRecorderSettingSpeedAuto, dashSpeedProfileAuto ? 1u : 0u, now);
    const uint8_t effectiveSpeedProfile = dashHandler
                                               ? dashClampSpeedProfileForHw(hwMode, (int)dashHandler->speedProfile)
                                               : dashClampSpeedProfileForHw(hwMode, dashManualSpeedProfile);
    dashRecorder.recordSetting(kRecorderSettingSpeedProfile, effectiveSpeedProfile, now);
    dashRecorder.recordSetting(kRecorderSettingApGate, apInjectionGate ? 1u : 0u, now);
    dashRecorder.recordSetting(kRecorderSettingSummonOnly, summonOnlyInjection ? 1u : 0u, now);
    dashRecorder.recordSetting(kRecorderSettingNagMode, dashNagMode, now);
    dashRecorder.recordSetting(kRecorderSettingReplayCount, replayCount, now);
    dashRecorder.recordSetting(kRecorderSettingOffsetSlew, hw3OffsetSlew ? 1u : 0u, now);
    dashRecorder.recordSetting(kRecorderSettingOffsetSlewRate, hw3SlewRate, now);
}

static void dashRecordEffectiveConfiguration()
{
    PluginLockGuard pluginGuard;
    DashDataGuard dataGuard;
    const uint8_t replayCount = pluginGetReplayCountLocked();
    const uint32_t now = millis();
    dashRecordEffectiveConfigurationAt(now, replayCount);
}

static uint32_t dashPluginStateHash(const char *value)
{
    uint32_t hash = 2166136261u;
    while (*value)
    {
        hash ^= (uint8_t)*value++;
        hash *= 16777619u;
    }
    return hash;
}

static void dashPluginStateKey(const char *filename, char *key, size_t keySize)
{
    snprintf(key, keySize, "plg_%08lx", (unsigned long)dashPluginStateHash(filename));
}

static void dashPluginOrderKey(const char *filename, char *key, size_t keySize)
{
    snprintf(key, keySize, "plo_%08lx", (unsigned long)dashPluginStateHash(filename));
}

static bool dashSaveAllPluginStates()
{
    struct PluginStateSnapshot
    {
        char filename[32];
        bool enabled;
    };
    PluginStateSnapshot snapshot[PLUGIN_MAX] = {};
    uint8_t snapshotCount = 0;
    {
        PluginLockGuard guard;
        snapshotCount = pluginCount;
        for (uint8_t i = 0; i < snapshotCount; i++)
        {
            strlcpy(snapshot[i].filename, pluginStore[i].filename, sizeof(snapshot[i].filename));
            snapshot[i].enabled = pluginStore[i].enabled;
        }
    }
    Preferences pluginPrefs;
    if (!pluginPrefs.begin(PREFS_NS, false))
        return false;

    for (uint8_t i = 0; i < snapshotCount; i++)
    {
        char key[13];
        dashPluginStateKey(snapshot[i].filename, key, sizeof(key));
        pluginPrefs.putBool(key, snapshot[i].enabled);
        dashPluginOrderKey(snapshot[i].filename, key, sizeof(key));
        pluginPrefs.putUChar(key, i);
    }
    return pluginPrefs.end();
}

static bool dashClearPluginState(const PluginData &plugin)
{
    Preferences pluginPrefs;
    if (!pluginPrefs.begin(PREFS_NS, false))
        return false;

    char key[13];
    dashPluginStateKey(plugin.filename, key, sizeof(key));
    pluginPrefs.remove(key);
    dashPluginOrderKey(plugin.filename, key, sizeof(key));
    pluginPrefs.remove(key);
    return pluginPrefs.end();
}

static void dashRestorePluginStates()
{
    Preferences pluginPrefs;
    if (!pluginPrefs.begin(PREFS_NS, false))
        return;

    bool missingOrder = false;
    for (uint8_t i = 0; i < pluginCount; i++)
    {
        char key[13];
        dashPluginStateKey(pluginStore[i].filename, key, sizeof(key));
        pluginStore[i].enabled = pluginPrefs.getBool(key, pluginStore[i].enabled);

        dashPluginOrderKey(pluginStore[i].filename, key, sizeof(key));
        if (pluginPrefs.isKey(key))
            pluginStore[i].priority = pluginPrefs.getUChar(key, i);
        else
        {
            pluginStore[i].priority = i;
            missingOrder = true;
        }
    }
    pluginPrefs.end();

    pluginSortByPriority();
    if (missingOrder)
        dashSaveAllPluginStates();
}

static void dashSchedulePluginStateSave(unsigned long delayMs)
{
    pluginStatesDirty = true;
    pluginStatesFlushAt = millis() + delayMs;
}

static void dashFlushPluginStatesIfDue()
{
    if (!pluginStatesDirty)
        return;

    unsigned long now = millis();
    if ((long)(now - pluginStatesFlushAt) < 0)
        return;

    if (dashSaveAllPluginStates())
        pluginStatesDirty = false;
    else
        pluginStatesFlushAt = now + 5000;
}

// MCP2515-only: fine-grained filter register reload on HW mode switch.
// Other builds use dashDriver->setFilters() in dashSwapHandler instead.
static void dashApplyFilters()
{
#if defined(DRIVER_ESP32_EXT_MCP2515)
    dashReapplyFiltersWithPlugins();
    dashLog("[CFG] Filters set for " + String(hwMode == 0 ? "LEGACY" : hwMode == 1 ? "HW3"
                                                                                   : "HW4"));
#endif
}

// Bus-off recovery (MCP2515 only — TWAI driver handles its own bus-off internally)
#if defined(DRIVER_ESP32_EXT_MCP2515)
static unsigned long lastEflgCheckMs = 0;
static void dashCheckBusHealth()
{
    if (!dashMcpDriver)
        return;
    if (millis() - lastEflgCheckMs < 5000)
        return;
    lastEflgCheckMs = millis();
    uint8_t eflg = dashMcpDriver->errorFlags();
    if (eflg & 0x20)
    {
        dashLog("[ERR] MCP2515 BUS-OFF -- recovering");
        bool recovered = dashMcpDriver->recover();
        delay(10);
        if (recovered)
        {
            dashApplyFilters();
            dashLog("[OK] MCP2515 recovered");
        }
        else
            dashLog("[ERR] MCP2515 recovery failed");
    }
}
#else
static void dashCheckBusHealth()
{
}
#endif
static WebServer server(80);
static TaskHandle_t webTaskHandle = nullptr;

static void dashSendBuffer(int code, const char *contentType, const char *data, size_t length)
{
#ifdef ESP_PLATFORM
    server.sendRaw(code, contentType, data, length);
#else
    (void)length;
    server.send(code, contentType, data);
#endif
}

static void dashSetPersistFailure(uint32_t generation)
{
    DashDataGuard guard;
    if (dashRecorder.generation() == generation)
    {
        dashIncidentPersistFailure = true;
        dashPersistFailureGeneration = generation;
    }
}

static void dashSetPersisted(uint32_t generation)
{
    DashDataGuard guard;
    if (dashRecorder.generation() == generation)
    {
        dashIncidentPersisted = true;
        dashPersistedGeneration = generation;
        dashIncidentPersistFailure = false;
        dashPersistFailureGeneration = 0;
    }
}

static void dashSetIncidentLifecycleError(const char *message, bool pressure = false)
{
    DashDataGuard guard;
    if (pressure)
        dashIncidentStoragePressure = true;
    else if (!message || !*message)
        dashIncidentStoragePressure = false;
    strlcpy(dashIncidentLastError, message ? message : "", sizeof(dashIncidentLastError));
}

static bool dashSpiffsInfo(size_t &totalBytes, size_t &usedBytes)
{
#ifdef ESP_PLATFORM
    return esp_spiffs_info(nullptr, &totalBytes, &usedBytes) == ESP_OK;
#else
    totalBytes = SPIFFS.totalBytes();
    usedBytes = SPIFFS.usedBytes();
    return totalBytes != 0;
#endif
}

static void dashRecorderBoardId(char *out, size_t outSize)
{
    if (!out || outSize == 0) return;
#ifdef ESP_PLATFORM
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK)
    {
        snprintf(out, outSize, "t2can-%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return;
    }
#endif
    strlcpy(out, "t2can-unknown", outSize);
}

static bool dashIncidentPathFor(uint32_t sequence, uint8_t slot, const char *suffix,
                                char *out, size_t outSize)
{
    if (!sequence || !suffix || !*suffix || !out || outSize == 0) return false;
    const int written = snprintf(out, outSize, "/incident-%lu-%u.%s",
                                 static_cast<unsigned long>(sequence),
                                 static_cast<unsigned>(slot), suffix);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

static bool dashPublicationFromNameWithSuffix(const char *name, const char *suffix,
                                              uint32_t &sequence, uint8_t &slot)
{
    if (!name || !suffix || strncmp(name, "/incident-", 10) != 0)
        return false;
    char *end = nullptr, *slotEnd = nullptr;
    const unsigned long value = strtoul(name + 10, &end, 10);
    if (end == name + 10 || !end || strncmp(end, "-", 1) != 0)
        return false;
    if (value > 0xFFFFFFFFUL)
        return false;
    const unsigned long slotValue = strtoul(end + 1, &slotEnd, 10);
    if (slotEnd == end + 1 || !slotEnd || *slotEnd != '.' ||
        strcmp(slotEnd + 1, suffix) != 0 || slotValue > 255UL)
        return false;
    sequence = static_cast<uint32_t>(value);
    slot = static_cast<uint8_t>(slotValue);
    return sequence != 0;
}

static bool dashPublicationFromName(const char *name, uint32_t &sequence, uint8_t &slot)
{
    return dashPublicationFromNameWithSuffix(name, "jsonl", sequence, slot);
}

static bool dashIncidentIdFromText(const String &text, uint32_t &sequence, uint8_t &slot)
{
    const char *value = text.c_str();
    if (!value || !*value) return false;
    char *end = nullptr, *slotEnd = nullptr;
    errno = 0;
    const unsigned long parsedSequence = strtoul(value, &end, 10);
    if (errno || end == value || !end || *end != '-' || parsedSequence == 0 ||
        parsedSequence > 0xFFFFFFFFUL)
        return false;
    const unsigned long parsedSlot = strtoul(end + 1, &slotEnd, 10);
    if (slotEnd == end + 1 || !slotEnd || *slotEnd || parsedSlot > 255UL)
        return false;
    sequence = static_cast<uint32_t>(parsedSequence);
    slot = static_cast<uint8_t>(parsedSlot);
    return true;
}

static bool dashNormalizeSha256(const String &input, char out[65])
{
    if (input.length() != 64) return false;
    for (size_t i = 0; i < 64; ++i)
    {
        char c = input[i];
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        out[i] = c;
    }
    out[64] = 0;
    return true;
}

static bool dashHashIncident(const char *path, size_t &sizeBytes, char sha256[65])
{
    sizeBytes = 0;
    sha256[0] = 0;
    File file = SPIFFS.open(path, "r");
    if (!file) return false;
#ifdef ESP_PLATFORM
    if (psa_crypto_init() != PSA_SUCCESS)
    {
        file.close();
        return false;
    }
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&operation, PSA_ALG_SHA_256) != PSA_SUCCESS)
    {
        file.close();
        return false;
    }
    uint8_t buffer[512];
    bool ok = true;
    for (;;)
    {
        const size_t count = file.read(buffer, sizeof(buffer));
        if (count == 0) break;
        sizeBytes += count;
        if (psa_hash_update(&operation, buffer, count) != PSA_SUCCESS)
        {
            ok = false;
            break;
        }
    }
    uint8_t digest[32] = {};
    size_t digestLength = 0;
    if (file.hasReadError() || !ok ||
        psa_hash_finish(&operation, digest, sizeof(digest), &digestLength) != PSA_SUCCESS ||
        digestLength != sizeof(digest))
    {
        psa_hash_abort(&operation);
        file.close();
        return false;
    }
    file.close();
    static constexpr char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(digest); ++i)
    {
        sha256[i * 2] = kHex[digest[i] >> 4];
        sha256[i * 2 + 1] = kHex[digest[i] & 0x0F];
    }
    sha256[64] = 0;
    return true;
#else
    file.close();
    return false;
#endif
}

static bool dashAckMatches(const char *ackPath, size_t sizeBytes, const char *sha256)
{
    File marker = SPIFFS.open(ackPath, "r");
    if (!marker) return false;
    const String actual = marker.readString();
    marker.close();
    char expected[112] = {};
    snprintf(expected, sizeof(expected), "size=%lu\nsha256=%s\n",
             static_cast<unsigned long>(sizeBytes), sha256);
    return actual == expected;
}

static bool dashWriteAck(const char *ackPath, size_t sizeBytes, const char *sha256)
{
    static constexpr char kAckTempPath[] = "/incident.ack.tmp";
    SPIFFS.remove(kAckTempPath);
    File marker = SPIFFS.open(kAckTempPath, "w");
    if (!marker) return false;
    char text[112] = {};
    const int length = snprintf(text, sizeof(text), "size=%lu\nsha256=%s\n",
                                static_cast<unsigned long>(sizeBytes), sha256);
    const bool wrote = length > 0 && static_cast<size_t>(length) < sizeof(text) &&
                       marker.write(reinterpret_cast<const uint8_t *>(text),
                                    static_cast<size_t>(length)) == static_cast<size_t>(length);
#ifdef ESP_PLATFORM
    const bool closed = marker.close();
#else
    marker.close();
    const bool closed = true;
#endif
    if (!wrote || !closed)
    {
        SPIFFS.remove(kAckTempPath);
        return false;
    }
    if (SPIFFS.exists(ackPath)) SPIFFS.remove(ackPath);
    if (!SPIFFS.rename(kAckTempPath, ackPath))
    {
        SPIFFS.remove(kAckTempPath);
        return false;
    }
    return true;
}

static void dashRefreshIncidentStats()
{
    uint16_t pending = 0, acknowledged = 0;
    File root = SPIFFS.open("/");
    if (root)
    {
        File entry = root.openNextFile();
        while (entry)
        {
            uint32_t sequence = 0;
            uint8_t slot = 0;
            const bool incident = dashPublicationFromName(entry.name(), sequence, slot);
            char incidentPath[64] = {};
            if (incident) strlcpy(incidentPath, entry.name(), sizeof(incidentPath));
            entry.close();
            if (incident)
            {
                char ackPath[64] = {};
                size_t verifiedSize = 0;
                char verifiedSha[65] = {};
                const bool delivered = dashIncidentPathFor(sequence, slot, "ack", ackPath, sizeof(ackPath)) &&
                                       SPIFFS.exists(ackPath) &&
                                       dashHashIncident(incidentPath, verifiedSize, verifiedSha) &&
                                       dashAckMatches(ackPath, verifiedSize, verifiedSha);
                uint16_t &count = delivered ? acknowledged : pending;
                if (count != UINT16_MAX) ++count;
            }
            entry = root.openNextFile();
        }
        root.close();
    }
    size_t totalBytes = 0, usedBytes = 0;
    const bool haveStorage = dashSpiffsInfo(totalBytes, usedBytes);
    const size_t freeBytes = haveStorage && totalBytes >= usedBytes ? totalBytes - usedBytes : 0;
    DashDataGuard guard;
    dashIncidentPendingCount = pending;
    dashIncidentAcknowledgedCount = acknowledged;
    dashIncidentStorageTotalBytes = totalBytes;
    dashIncidentStorageFreeBytes = freeBytes;
    dashIncidentStoragePressure = haveStorage && freeBytes < kDashIncidentSafetyFreeBytes;
}

static bool dashFindLatestIncidentPath()
{
    File root = SPIFFS.open("/");
    if (!root)
        return false;
    bool found = false;
    uint32_t bestSequence = 0;
    uint8_t bestSlot = 0;
    char bestPath[sizeof(dashIncidentPath)] = {};
    File entry = root.openNextFile();
    while (entry)
    {
        uint32_t sequence = 0;
        uint8_t slot = 0;
        const char *name = entry.name();
        if (dashPublicationFromName(name, sequence, slot) &&
            (!found || sequence > bestSequence ||
             (sequence == bestSequence && slot > bestSlot)))
        {
            strlcpy(bestPath, name, sizeof(bestPath));
            bestSequence = sequence;
            bestSlot = slot;
            found = true;
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
    if (!found && SPIFFS.exists("/incident.jsonl"))
    {
        strlcpy(dashIncidentPath, "/incident.jsonl", sizeof(dashIncidentPath));
        return true;
    }
    if (!found) return false;
    strlcpy(dashIncidentPath, bestPath, sizeof(dashIncidentPath));
    return true;
}

static bool dashChooseIncidentPath(char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return false;
    File root = SPIFFS.open("/");
    if (!root)
        return false;
    uint32_t maxKnownSequence = 0;
    File entry = root.openNextFile();
    while (entry)
    {
        uint32_t sequence = 0;
        uint8_t slot = 0;
        if ((dashPublicationFromName(entry.name(), sequence, slot) ||
             dashPublicationFromNameWithSuffix(entry.name(), "ack", sequence, slot)) &&
            sequence > maxKnownSequence)
            maxKnownSequence = sequence;
        entry.close();
        entry = root.openNextFile();
    }
    root.close();

    uint32_t storedSequence = 0;
    bool storedValid = false;
    bool saved = false;
    {
        DashPrefsGuard guard;
        if (!prefs.begin(PREFS_NS, false)) return false;
        const String stored = prefs.getString("inc_seq", "0");
        char *end = nullptr;
        errno = 0;
        const unsigned long parsed = strtoul(stored.c_str(), &end, 10);
        storedValid = !errno && end != stored.c_str() && end && !*end && parsed <= 0xFFFFFFFFUL;
        if (!storedValid)
        {
            prefs.end();
            return false;
        }
        if (storedValid) storedSequence = static_cast<uint32_t>(parsed);
        const uint32_t previous = storedSequence > maxKnownSequence ? storedSequence : maxKnownSequence;
        if (previous == 0xFFFFFFFFu)
        {
            prefs.end();
            return false;
        }
        const uint32_t nextSequence = previous + 1;
        const bool wrote = prefs.putString("inc_seq", String(static_cast<unsigned long>(nextSequence)));
        const bool closed = prefs.end();
        saved = wrote && closed;
        if (!saved) return false;
        if (!dashIncidentPathFor(nextSequence, 0, "jsonl", out, outSize)) return false;
    }
    if (!saved || SPIFFS.exists(out))
    {
        out[0] = 0;
        return false;
    }
    return true;
}

static void dashMigrateLegacyIncident()
{
    if (!SPIFFS.exists("/incident.jsonl")) return;
    char path[64] = {};
    if (dashChooseIncidentPath(path, sizeof(path)) && SPIFFS.rename("/incident.jsonl", path))
        strlcpy(dashIncidentPath, path, sizeof(dashIncidentPath));
    else
        dashSetIncidentLifecycleError("legacy_migration_failed");
}

struct DashIncidentSummary
{
    uint32_t sequence = 0;
    uint8_t slot = 0;
    size_t sizeBytes = 0;
    bool acknowledged = false;
    char path[64] = {};
};

static bool dashIncidentBefore(uint32_t sequence, uint8_t slot,
                               const DashIncidentSummary &other)
{
    return sequence < other.sequence || (sequence == other.sequence && slot < other.slot);
}

static bool dashFindOldestAcknowledgedIncident(DashIncidentSummary &out)
{
    bool found = false;
    File root = SPIFFS.open("/");
    if (!root) return false;
    File entry = root.openNextFile();
    while (entry)
    {
        uint32_t sequence = 0;
        uint8_t slot = 0;
        const bool incident = dashPublicationFromName(entry.name(), sequence, slot);
        const size_t sizeBytes = incident ? entry.size() : 0;
        char path[64] = {};
        if (incident) strlcpy(path, entry.name(), sizeof(path));
        entry.close();
        if (incident)
        {
            char ackPath[64] = {};
            size_t verifiedSize = 0;
            char verifiedSha[65] = {};
            const bool acknowledged = dashIncidentPathFor(sequence, slot, "ack", ackPath, sizeof(ackPath)) &&
                                      SPIFFS.exists(ackPath) &&
                                      dashHashIncident(path, verifiedSize, verifiedSha) &&
                                      verifiedSize == sizeBytes &&
                                      dashAckMatches(ackPath, verifiedSize, verifiedSha);
            if (acknowledged && (!found || dashIncidentBefore(sequence, slot, out)))
            {
                out.sequence = sequence;
                out.slot = slot;
                out.sizeBytes = sizeBytes;
                out.acknowledged = true;
                strlcpy(out.path, path, sizeof(out.path));
                found = true;
            }
        }
        entry = root.openNextFile();
    }
    root.close();
    return found;
}

static void dashTrimAckTombstones()
{
    size_t tombstones = 0;
    File root = SPIFFS.open("/");
    if (!root) return;
    File entry = root.openNextFile();
    while (entry)
    {
        uint32_t sequence = 0;
        uint8_t slot = 0;
        const bool ack = dashPublicationFromNameWithSuffix(entry.name(), "ack", sequence, slot);
        entry.close();
        if (ack)
        {
            char incidentPath[64] = {};
            if (dashIncidentPathFor(sequence, slot, "jsonl", incidentPath, sizeof(incidentPath)) &&
                !SPIFFS.exists(incidentPath))
                ++tombstones;
        }
        entry = root.openNextFile();
    }
    root.close();

    while (tombstones > kDashIncidentAckTombstoneLimit)
    {
        bool found = false;
        uint32_t oldestSequence = 0;
        uint8_t oldestSlot = 0;
        root = SPIFFS.open("/");
        if (!root) return;
        entry = root.openNextFile();
        while (entry)
        {
            uint32_t sequence = 0;
            uint8_t slot = 0;
            const bool ack = dashPublicationFromNameWithSuffix(entry.name(), "ack", sequence, slot);
            entry.close();
            if (ack)
            {
                char incidentPath[64] = {};
                const bool orphan = dashIncidentPathFor(sequence, slot, "jsonl", incidentPath, sizeof(incidentPath)) &&
                                    !SPIFFS.exists(incidentPath);
                if (orphan && (!found || sequence < oldestSequence ||
                               (sequence == oldestSequence && slot < oldestSlot)))
                {
                    oldestSequence = sequence;
                    oldestSlot = slot;
                    found = true;
                }
            }
            entry = root.openNextFile();
        }
        root.close();
        if (!found) return;
        char ackPath[64] = {};
        if (!dashIncidentPathFor(oldestSequence, oldestSlot, "ack", ackPath, sizeof(ackPath)) ||
            !SPIFFS.remove(ackPath))
            return;
        --tombstones;
    }
}

static size_t dashEstimatedIncidentBytes(size_t rawCount, size_t stateCount, size_t configCount)
{
    constexpr size_t kBaseBytes = 4096;
    constexpr size_t kRawLineBytes = 192;
    constexpr size_t kStateLineBytes = 128;
    constexpr size_t kConfigLineBytes = 96;
    if (rawCount > (SIZE_MAX - kBaseBytes) / kRawLineBytes) return SIZE_MAX;
    size_t total = kBaseBytes + rawCount * kRawLineBytes;
    if (stateCount > (SIZE_MAX - total) / kStateLineBytes) return SIZE_MAX;
    total += stateCount * kStateLineBytes;
    if (configCount > (SIZE_MAX - total) / kConfigLineBytes) return SIZE_MAX;
    return total + configCount * kConfigLineBytes;
}

static bool dashEnsureIncidentSpace(size_t requiredFreeBytes)
{
    size_t totalBytes = 0, usedBytes = 0;
    if (!dashSpiffsInfo(totalBytes, usedBytes) || totalBytes < usedBytes ||
        requiredFreeBytes > totalBytes)
    {
        dashRefreshIncidentStats();
        dashSetIncidentLifecycleError("storage_info_failed", true);
        return false;
    }
    size_t freeBytes = totalBytes - usedBytes;
    while (freeBytes < requiredFreeBytes)
    {
        DashIncidentSummary oldest;
        if (!dashFindOldestAcknowledgedIncident(oldest))
        {
            dashRefreshIncidentStats();
            dashSetIncidentLifecycleError("storage_full_unacknowledged", true);
            return false;
        }
        if (!SPIFFS.remove(oldest.path))
        {
            dashRefreshIncidentStats();
            dashSetIncidentLifecycleError("acknowledged_eviction_failed", true);
            return false;
        }
        {
            DashDataGuard guard;
            ++dashIncidentEvictions;
        }
        if (!dashSpiffsInfo(totalBytes, usedBytes) || totalBytes < usedBytes)
        {
            dashRefreshIncidentStats();
            dashSetIncidentLifecycleError("storage_info_failed", true);
            return false;
        }
        freeBytes = totalBytes - usedBytes;
    }
    dashTrimAckTombstones();
    dashSetIncidentLifecycleError("");
    dashRefreshIncidentStats();
    return true;
}

struct DashPersistLease
{
    explicit DashPersistLease(uint32_t value) : generation(value) {}
    ~DashPersistLease() noexcept
    {
        if (!completed)
        {
            try { SPIFFS.remove("/incident.tmp"); } catch (...) {}
            try { dashSetPersistFailure(generation); } catch (...) {}
            try
            {
                DashDataGuard guard;
                dashPersistInProgress = false;
            }
            catch (...) {}
        }
    }
    void complete() noexcept
    {
        completed = true;
        try
        {
            DashDataGuard guard;
            dashPersistInProgress = false;
        }
        catch (...) {}
    }
    uint32_t generation;
    bool completed = false;
};

static bool dashWriteIncidentText(File &file, const char *text)
{
    if (!text)
        return false;
    const size_t length = strlen(text);
    return file.write(reinterpret_cast<const uint8_t *>(text), length) == length;
}

// Called only from the HTTP/web maintenance context. CAN callbacks only fill
// the bounded rings; they never touch SPIFFS.
static bool dashPersistFrozenIncident()
{
    size_t rawLimit = 0, stateLimit = 0, configLimit = 0;
    size_t rawCapacity = 0, stateCapacity = 0;
    uint32_t generation = 0, triggerMs = 0, coverageMs = 0, stateCoverageMs = 0;
    uint32_t rawDrops = 0, stateDrops = 0, stateProtectedDrops = 0;
    bool stateTargetReady = false;
    bool usingPsram = false;
    char triggerReason[16] = {};
    Chassis::EventRecorder::EffectiveSetting config[32] = {};
#ifdef ESP_PLATFORM
    const int resetReason = static_cast<int>(RuntimeDiagnostics::bootResetReason);
    const bool psramVerified = RuntimeDiagnostics::psramVerified.load(std::memory_order_relaxed);
    const unsigned long psramBytes = static_cast<unsigned long>(RuntimeDiagnostics::systemInfo.psramBytes);
#else
    const int resetReason = 0;
    const bool psramVerified = false;
    const unsigned long psramBytes = 0UL;
#endif
    {
        DashDataGuard guard;
        if (!dashRecorder.frozen()) return false;
        generation = dashRecorder.generation();
        if (dashPersistedGeneration == generation || dashPersistInProgress) return false;
        dashPersistInProgress = true;
        rawLimit = dashRecorder.rawCount();
        stateLimit = dashRecorder.stateCount();
        rawCapacity = dashRecorder.rawCapacity();
        stateCapacity = dashRecorder.stateCapacity();
        coverageMs = dashRecorder.coverageMs();
        stateCoverageMs = dashRecorder.stateHistoryCoverageMs();
        stateTargetReady = dashRecorder.stateTargetWindowReady();
        rawDrops = dashRecorder.rawDrops();
        stateDrops = dashRecorder.stateDrops();
        stateProtectedDrops = dashRecorder.stateProtectedDrops();
        usingPsram = dashRecorder.usingPsram();
        triggerMs = dashRecorder.triggerMs();
        strncpy(triggerReason, dashRecorder.reason(), sizeof(triggerReason) - 1);
        configLimit = dashRecorder.effectiveSettingCount();
        if (configLimit > sizeof(config) / sizeof(config[0]))
            configLimit = sizeof(config) / sizeof(config[0]);
        for (size_t i = 0; i < configLimit; ++i)
            dashRecorder.effectiveSetting(i, config[i]);
    }
    DashPersistLease lease(generation);

    const size_t estimatedBytes = dashEstimatedIncidentBytes(rawLimit, stateLimit, configLimit);
    if (estimatedBytes == SIZE_MAX || estimatedBytes > SIZE_MAX - kDashIncidentSafetyFreeBytes ||
        !dashEnsureIncidentSpace(estimatedBytes + kDashIncidentSafetyFreeBytes))
    {
        dashSetPersistFailure(generation);
        return false;
    }

    char finalPath[sizeof(dashIncidentPath)] = {};
    uint32_t publicationSequence = 0;
    uint8_t publicationSlot = 0;
    if (!dashChooseIncidentPath(finalPath, sizeof(finalPath)) ||
        !dashPublicationFromName(finalPath, publicationSequence, publicationSlot))
    {
        dashSetIncidentLifecycleError("incident_sequence_failed");
        dashSetPersistFailure(generation);
        return false;
    }
    char boardId[32] = {};
    dashRecorderBoardId(boardId, sizeof(boardId));

    File tmp = SPIFFS.open("/incident.tmp", "w");
    if (!tmp)
    {
        dashSetPersistFailure(generation);
        return false;
    }
    char line[768];
    snprintf(line, sizeof(line), "{\"schema\":\"t2can-flight-recorder-v1\",\"type\":\"header\",\"boardId\":\"%s\",\"incidentId\":\"%lu-%u\",\"sequence\":%lu,\"firmware\":\"%s\",\"build\":\"%s %s\",\"reset\":%d,\"psramVerified\":%s,\"psramBytes\":%lu,\"storage\":\"SPIFFS\",\"trigger\":\"%s\",\"triggerMs\":%lu,\"generation\":%lu,\"rawCapacity\":%u,\"stateCapacity\":%u,\"coverageMs\":%lu,\"stateCoverageMs\":%lu,\"stateTargetMs\":%lu,\"stateTargetReady\":%s,\"rawDrops\":%lu,\"stateDrops\":%lu,\"stateProtectedDrops\":%lu,\"usingPsram\":%s}\n",
              boardId, static_cast<unsigned long>(publicationSequence),
              static_cast<unsigned>(publicationSlot), static_cast<unsigned long>(publicationSequence),
              FIRMWARE_VERSION, __DATE__, __TIME__, resetReason,
              psramVerified ? "true" : "false", psramBytes, triggerReason,
              static_cast<unsigned long>(triggerMs), static_cast<unsigned long>(generation),
              static_cast<unsigned>(rawCapacity), static_cast<unsigned>(stateCapacity),
              static_cast<unsigned long>(coverageMs), static_cast<unsigned long>(stateCoverageMs),
              static_cast<unsigned long>(Chassis::EventRecorder::StateTargetWindowMs),
              stateTargetReady ? "true" : "false", static_cast<unsigned long>(rawDrops),
              static_cast<unsigned long>(stateDrops), static_cast<unsigned long>(stateProtectedDrops),
              usingPsram ? "true" : "false");
    if (!dashWriteIncidentText(tmp, line))
    {
        tmp.close();
        dashSetPersistFailure(generation);
        return false;
    }
    for (size_t i = 0; i < configLimit; ++i)
    {
        snprintf(line, sizeof(line), "{\"type\":\"config\",\"setting\":%u,\"value\":%lu}\n",
                 static_cast<unsigned>(config[i].setting),
                 static_cast<unsigned long>(config[i].value));
        if (!dashWriteIncidentText(tmp, line))
        {
            tmp.close();
            dashSetPersistFailure(generation);
            return false;
        }
    }
    for (size_t i = 0; i < stateLimit; ++i)
    {
        Chassis::EventRecorder::StateRecord state;
        {
            DashDataGuard guard;
            if (!dashRecorder.frozen() || dashRecorder.generation() != generation ||
                !dashRecorder.stateRecord(i, state))
            {
                tmp.close();
                dashSetPersistFailure(generation);
                return false;
            }
        }
        snprintf(line, sizeof(line), "{\"type\":\"state\",\"ms\":%lu,\"kind\":%u,\"reason\":%u,\"value\":%u,\"value32\":%lu}\n",
                 static_cast<unsigned long>(state.ms), static_cast<unsigned>(state.kind),
                 static_cast<unsigned>(state.reason), static_cast<unsigned>(state.value),
                 static_cast<unsigned long>(state.value32));
        if (!dashWriteIncidentText(tmp, line))
        {
            tmp.close();
            dashSetPersistFailure(generation);
            return false;
        }
    }
    for (size_t i = 0; i < rawLimit; ++i)
    {
        Chassis::EventRecorder::RawRecord raw;
        {
            DashDataGuard guard;
            if (!dashRecorder.frozen() || dashRecorder.generation() != generation ||
                !dashRecorder.rawRecord(i, raw))
            {
                tmp.close();
                dashSetPersistFailure(generation);
                return false;
            }
        }
        snprintf(line, sizeof(line), "{\"type\":\"raw\",\"ms\":%lu,\"direction\":\"%s\",\"txOk\":%s,\"txAttempted\":%s,\"busMask\":%u,\"physicalBus\":%u,\"id\":%lu,\"dlc\":%u,\"data\":\"",
                 static_cast<unsigned long>(raw.ms),
                 raw.direction == Chassis::EventRecorder::Direction::Tx ? "tx" : "rx",
                 raw.direction == Chassis::EventRecorder::Direction::Tx ? (raw.txOk ? "true" : "false") : "true",
                 raw.direction == Chassis::EventRecorder::Direction::Tx ? (raw.txAttempted ? "true" : "false") : "true",
                 static_cast<unsigned>(raw.frame.bus), static_cast<unsigned>(raw.frame.physicalBus),
                 static_cast<unsigned long>(raw.frame.id),
                 static_cast<unsigned>(raw.frame.dlc));
        if (!dashWriteIncidentText(tmp, line))
        {
            tmp.close();
            dashSetPersistFailure(generation);
            return false;
        }
        for (uint8_t j = 0; j < raw.frame.dlc && j < 8; ++j)
        {
            snprintf(line, sizeof(line), "%02X", static_cast<unsigned>(raw.frame.data[j]));
            if (!dashWriteIncidentText(tmp, line))
            {
                tmp.close();
                dashSetPersistFailure(generation);
                return false;
            }
        }
        if (!dashWriteIncidentText(tmp, "\"}\n"))
        {
            tmp.close();
            dashSetPersistFailure(generation);
            return false;
        }
    }
#ifdef ESP_PLATFORM
    const bool closeOk = tmp.close();
#else
    tmp.close();
    const bool closeOk = true;
#endif
    if (!closeOk || !SPIFFS.rename("/incident.tmp", finalPath))
    {
        dashSetPersistFailure(generation);
        return false;
    }
    strlcpy(dashIncidentPath, finalPath, sizeof(dashIncidentPath));
    dashSetPersisted(generation);
    dashRefreshIncidentStats();
    lease.complete();
    return true;
}

static void handleRoot()
{
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "max-age=3600");
#ifdef ESP_PLATFORM
    server.sendRaw(200, "text/html",
                   reinterpret_cast<const char *>(DASH_HTML_GZ),
                   DASH_HTML_GZ_LEN);
#else
    server.send_P(200, "text/html", reinterpret_cast<const char *>(DASH_HTML_GZ), DASH_HTML_GZ_LEN);
#endif
}

// RAM-only event capture: all allocations and HTTP writes happen outside the CAN lock.
static void handleDiagnosticDetails()
{
    Chassis::TelemetrySnapshot t;
    ExperimentalAssist::DryRunSnapshot dryRun;
    bool enabled = false, frozen = false, usingPsram = false, persisted = false;
    size_t count = 0, rawCount = 0, stateCount = 0, rawCapacity = 0, stateCapacity = 0;
    uint32_t triggerMs = 0, coverageMs = 0, stateCoverageMs = 0, drops = 0;
    uint32_t rawDrops = 0, stateDrops = 0, stateProtectedDrops = 0, generation = 0;
    uint32_t incidentEvictions = 0;
    uint16_t pendingIncidents = 0, acknowledgedIncidents = 0;
    size_t incidentStorageTotal = 0, incidentStorageFree = 0;
    bool stateTargetReady = false, incidentStoragePressure = false;
    char incidentLastError[sizeof(dashIncidentLastError)] = {};
    char reason[16] = {};
#ifdef ESP_PLATFORM
    bool psramVerified = false;
    unsigned long psramBytes = 0, psramProbeBytes = 0;
#endif
    {
        DashDataGuard guard;
        const uint32_t now = millis();
        t = dashTelemetry.snapshot(now);
        dashRecorder.tick(now);
        enabled = dashRecorder.enabled(); frozen = dashRecorder.frozen();
        count = dashRecorder.count(); triggerMs = dashRecorder.triggerMs();
        rawCount = dashRecorder.rawCount(); stateCount = dashRecorder.stateCount();
        rawCapacity = dashRecorder.rawCapacity(); stateCapacity = dashRecorder.stateCapacity();
        coverageMs = dashRecorder.coverageMs(); stateCoverageMs = dashRecorder.stateHistoryCoverageMs();
        stateTargetReady = dashRecorder.stateTargetWindowReady(); rawDrops = dashRecorder.rawDrops();
        stateDrops = dashRecorder.stateDrops(); stateProtectedDrops = dashRecorder.stateProtectedDrops();
        drops = dashRecorder.drops();
        usingPsram = dashRecorder.usingPsram(); generation = dashRecorder.generation();
        persisted = dashIncidentPersisted && dashPersistedGeneration == generation;
        pendingIncidents = dashIncidentPendingCount;
        acknowledgedIncidents = dashIncidentAcknowledgedCount;
        incidentStorageTotal = dashIncidentStorageTotalBytes;
        incidentStorageFree = dashIncidentStorageFreeBytes;
        incidentStoragePressure = dashIncidentStoragePressure;
        incidentEvictions = dashIncidentEvictions;
        dryRun = dashHandsOn247DryRun.snapshot();
        strlcpy(incidentLastError, dashIncidentLastError, sizeof(incidentLastError));
        strncpy(reason, dashRecorder.reason(), sizeof(reason) - 1);
#ifdef ESP_PLATFORM
        psramVerified = RuntimeDiagnostics::psramVerified.load(std::memory_order_relaxed);
        psramBytes = static_cast<unsigned long>(RuntimeDiagnostics::systemInfo.psramBytes);
        psramProbeBytes = static_cast<unsigned long>(RuntimeDiagnostics::psramProbeBytes.load(std::memory_order_relaxed));
#endif
    }
    char response[2450];
    BoundedTextWriter json(response, sizeof(response));
    json.appendf("{\"bms\":{\"hvSeen\":%s,\"voltage\":%.2f,\"current\":%.1f,"
        "\"socSeen\":%s,\"soc\":%.1f,\"thermalSeen\":%s,\"minC\":%d,\"maxC\":%d},"
        "\"das\":{\"seen\":%s,\"laneChange\":%u,\"sideWarning\":%u,\"sideCollisionAvoid\":%u,\"laneDepartureWarning\":%u,\"forwardWarning\":%u,"
        "\"activationFailureSeen\":%s,\"activationFailure\":%u,\"autosteerSeen\":%s,\"autosteerEnabled\":%s,\"trackModeSeen\":%s,\"trackMode\":%u,\"tractionControl\":%u,"
        "\"limitSeen\":%s,\"limitKph\":%u},"
        "\"event\":{\"enabled\":%s,\"frozen\":%s,\"count\":%u,\"rawCount\":%u,\"stateCount\":%u,"
        "\"rawCapacity\":%u,\"stateCapacity\":%u,\"coverageMs\":%lu,\"stateCoverageMs\":%lu,"
        "\"stateTargetMs\":%lu,\"stateTargetReady\":%s,\"drops\":%lu,\"rawDrops\":%lu,"
        "\"stateDrops\":%lu,\"stateProtectedDrops\":%lu,\"usingPsram\":%s,"
        "\"generation\":%lu,\"persisted\":%s,\"reason\":\"%s\",\"triggerMs\":%lu},"
        "\"incidentStore\":{\"pending\":%u,\"acknowledged\":%u,\"totalBytes\":%lu,\"freeBytes\":%lu,"
        "\"pressure\":%s,\"evictions\":%lu,\"lastError\":\"%s\"},"
        "\"handsOn247DryRun\":{\"enabled\":%s,\"frames247\":%lu,\"frames3e9\":%lu,\"nearbyEvents\":%lu},"
        "\"psram\":{\"verified\":%s,\"totalBytes\":%lu,\"probeBytes\":%lu}}",
        t.bmsHvSeen ? "true" : "false", t.packVoltageV, t.packCurrentA,
        t.bmsSocSeen ? "true" : "false", t.socPercent,
        t.bmsThermalSeen ? "true" : "false", int(t.tempMinC), int(t.tempMaxC),
        t.dasSeen ? "true" : "false", unsigned(t.laneChange), unsigned(t.sideWarning),
        unsigned(t.sideCollisionAvoid), unsigned(t.laneDepartureWarning), unsigned(t.forwardWarning),
        t.dasStatus2Seen ? "true" : "false", unsigned(t.activationFailureStatus),
        t.dasSettingsSeen ? "true" : "false",
        t.autosteerEnabled ? "true" : "false", t.diModesSeen ? "true" : "false",
        unsigned(t.trackModeState), unsigned(t.tractionControlMode),
        t.visionLimitSeen ? "true" : "false", unsigned(t.visionLimitKph),
        enabled ? "true" : "false", frozen ? "true" : "false", unsigned(count), unsigned(rawCount),
        unsigned(stateCount), unsigned(rawCapacity), unsigned(stateCapacity),
        static_cast<unsigned long>(coverageMs), static_cast<unsigned long>(stateCoverageMs),
        static_cast<unsigned long>(Chassis::EventRecorder::StateTargetWindowMs), stateTargetReady ? "true" : "false",
        static_cast<unsigned long>(drops), static_cast<unsigned long>(rawDrops),
        static_cast<unsigned long>(stateDrops), static_cast<unsigned long>(stateProtectedDrops),
        usingPsram ? "true" : "false", static_cast<unsigned long>(generation), persisted ? "true" : "false", reason,
        static_cast<unsigned long>(triggerMs),
        static_cast<unsigned>(pendingIncidents), static_cast<unsigned>(acknowledgedIncidents),
        static_cast<unsigned long>(incidentStorageTotal), static_cast<unsigned long>(incidentStorageFree),
        incidentStoragePressure ? "true" : "false", static_cast<unsigned long>(incidentEvictions), incidentLastError,
        dryRun.enabled ? "true" : "false", static_cast<unsigned long>(dryRun.frames247),
        static_cast<unsigned long>(dryRun.frames3e9), static_cast<unsigned long>(dryRun.nearbyEvents),
#ifdef ESP_PLATFORM
        psramVerified ? "true" : "false", psramBytes, psramProbeBytes
#else
        "false", 0UL, 0UL
#endif
    );
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", response);
}

static void handleEventControl()
{
    const String action = server.arg("action");
    bool ok = true;
    bool busy = false;
    bool recordEffective = false;
    {
        DashDataGuard guard;
        const bool mutatesGeneration = action == "enable" || action == "disable" || action == "clear";
        busy = mutatesGeneration && dashPersistInProgress;
        if (busy)
            ok = false;
        else if (action == "enable") { dashRecorder.enable(true); recordEffective = true; }
        else if (action == "disable") dashRecorder.enable(false);
        else if (action == "clear") { dashRecorder.clear(); recordEffective = true; }
        else if (action == "mark") ok = dashRecorder.mark(Chassis::EventRecorder::Trigger::Manual, millis());
        else ok = false;
    }
    if (recordEffective)
        dashRecordEffectiveConfiguration();
    if (busy)
    {
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"Incident persistence in progress\"}");
        return;
    }
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"Invalid action or recorder not armed\"}");
}

static bool dashRequireRecorderAuth()
{
    if (server.authenticate(DASH_OTA_USER, DASH_OTA_PASS)) return true;
    server.requestAuthentication();
    return false;
}

static size_t dashCollectIncidentPage(uint32_t afterSequence, DashIncidentSummary *out,
                                      size_t limit, size_t &eligibleCount)
{
    eligibleCount = 0;
    size_t selected = 0;
    File root = SPIFFS.open("/");
    if (!root) return 0;
    File entry = root.openNextFile();
    while (entry)
    {
        uint32_t sequence = 0;
        uint8_t slot = 0;
        const bool incident = dashPublicationFromName(entry.name(), sequence, slot);
        const size_t sizeBytes = incident ? entry.size() : 0;
        char path[64] = {};
        if (incident) strlcpy(path, entry.name(), sizeof(path));
        entry.close();
        if (incident && sequence > afterSequence)
        {
            ++eligibleCount;
            size_t insertAt = 0;
            while (insertAt < selected && !dashIncidentBefore(sequence, slot, out[insertAt]))
                ++insertAt;
            if (selected < limit)
            {
                for (size_t i = selected; i > insertAt; --i) out[i] = out[i - 1];
                ++selected;
            }
            else if (insertAt < limit)
            {
                for (size_t i = limit - 1; i > insertAt; --i) out[i] = out[i - 1];
            }
            else
            {
                entry = root.openNextFile();
                continue;
            }
            DashIncidentSummary &summary = out[insertAt];
            summary.sequence = sequence;
            summary.slot = slot;
            summary.sizeBytes = sizeBytes;
            strlcpy(summary.path, path, sizeof(summary.path));
            char ackPath[64] = {};
            summary.acknowledged = dashIncidentPathFor(sequence, slot, "ack", ackPath, sizeof(ackPath)) &&
                                   SPIFFS.exists(ackPath);
        }
        entry = root.openNextFile();
    }
    root.close();
    return selected;
}

static void handleEventList()
{
    if (!dashRequireRecorderAuth()) return;
    uint32_t afterSequence = 0;
    if (server.hasArg("after"))
    {
        char *end = nullptr;
        errno = 0;
        const String value = server.arg("after");
        const unsigned long parsed = strtoul(value.c_str(), &end, 10);
        if (errno || end == value.c_str() || !end || *end || parsed > 0xFFFFFFFFUL)
        {
            server.send(400, "application/json", "{\"error\":\"Invalid after sequence\"}");
            return;
        }
        afterSequence = static_cast<uint32_t>(parsed);
    }

    DashIncidentSummary incidents[kDashIncidentListLimit] = {};
    size_t eligibleCount = 0;
    const size_t count = dashCollectIncidentPage(afterSequence, incidents,
                                                 kDashIncidentListLimit, eligibleCount);
    dashRefreshIncidentStats();

    char boardId[32] = {};
    dashRecorderBoardId(boardId, sizeof(boardId));
    uint16_t pending = 0, acknowledged = 0;
    uint32_t evictions = 0;
    size_t totalBytes = 0, freeBytes = 0;
    bool pressure = false;
    char lastError[sizeof(dashIncidentLastError)] = {};
    {
        DashDataGuard guard;
        pending = dashIncidentPendingCount;
        acknowledged = dashIncidentAcknowledgedCount;
        evictions = dashIncidentEvictions;
        totalBytes = dashIncidentStorageTotalBytes;
        freeBytes = dashIncidentStorageFreeBytes;
        pressure = dashIncidentStoragePressure;
        strlcpy(lastError, dashIncidentLastError, sizeof(lastError));
    }

    String response;
    response.reserve(6144);
    response += "{\"schema\":\"t2can-incident-list-v1\",\"boardId\":\"";
    response += boardId;
    response += "\",\"pending\":";
    response += String(static_cast<unsigned>(pending));
    response += ",\"acknowledged\":";
    response += String(static_cast<unsigned>(acknowledged));
    response += ",\"storageTotalBytes\":";
    response += String(static_cast<unsigned long>(totalBytes));
    response += ",\"storageFreeBytes\":";
    response += String(static_cast<unsigned long>(freeBytes));
    response += ",\"storagePressure\":";
    response += pressure ? "true" : "false";
    response += ",\"evictions\":";
    response += String(static_cast<unsigned long>(evictions));
    response += ",\"lastError\":\"";
    response += lastError;
    response += "\",\"truncated\":";
    response += eligibleCount > count ? "true" : "false";
    response += ",\"incidents\":[";
    for (size_t i = 0; i < count; ++i)
    {
        size_t sizeBytes = 0;
        char sha256[65] = {};
        const bool hashed = dashHashIncident(incidents[i].path, sizeBytes, sha256);
        char ackPath[64] = {};
        incidents[i].acknowledged = hashed &&
            dashIncidentPathFor(incidents[i].sequence, incidents[i].slot, "ack",
                                ackPath, sizeof(ackPath)) &&
            SPIFFS.exists(ackPath) && dashAckMatches(ackPath, sizeBytes, sha256);
        if (i) response += ',';
        response += "{\"id\":\"";
        response += String(static_cast<unsigned long>(incidents[i].sequence));
        response += '-';
        response += String(static_cast<unsigned>(incidents[i].slot));
        response += "\",\"sequence\":";
        response += String(static_cast<unsigned long>(incidents[i].sequence));
        response += ",\"slot\":";
        response += String(static_cast<unsigned>(incidents[i].slot));
        response += ",\"size\":";
        response += String(static_cast<unsigned long>(hashed ? sizeBytes : incidents[i].sizeBytes));
        response += ",\"sha256\":";
        if (hashed)
        {
            response += '"';
            response += sha256;
            response += '"';
        }
        else
            response += "null";
        response += ",\"acknowledged\":";
        response += incidents[i].acknowledged ? "true" : "false";
        response += '}';
    }
    response += "]}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", response);
}

static void handleEventAck()
{
    if (!dashRequireRecorderAuth()) return;
    if (!server.hasArg("id") || !server.hasArg("size") || !server.hasArg("sha256"))
    {
        server.send(400, "application/json", "{\"error\":\"id, size and sha256 are required\"}");
        return;
    }
    uint32_t sequence = 0;
    uint8_t slot = 0;
    if (!dashIncidentIdFromText(server.arg("id"), sequence, slot))
    {
        server.send(400, "application/json", "{\"error\":\"Invalid incident id\"}");
        return;
    }
    const String sizeText = server.arg("size");
    char *sizeEnd = nullptr;
    errno = 0;
    const unsigned long submittedSize = strtoul(sizeText.c_str(), &sizeEnd, 10);
    char submittedSha[65] = {};
    if (errno || sizeEnd == sizeText.c_str() || !sizeEnd || *sizeEnd ||
        !dashNormalizeSha256(server.arg("sha256"), submittedSha))
    {
        server.send(400, "application/json", "{\"error\":\"Invalid size or sha256\"}");
        return;
    }
    char incidentPath[64] = {}, ackPath[64] = {};
    if (!dashIncidentPathFor(sequence, slot, "jsonl", incidentPath, sizeof(incidentPath)) ||
        !dashIncidentPathFor(sequence, slot, "ack", ackPath, sizeof(ackPath)))
    {
        server.send(400, "application/json", "{\"error\":\"Invalid incident id\"}");
        return;
    }
    if (!SPIFFS.exists(incidentPath))
    {
        if (SPIFFS.exists(ackPath) && dashAckMatches(ackPath, submittedSize, submittedSha))
        {
            server.send(200, "application/json", "{\"ok\":true,\"alreadyAcknowledged\":true}");
            return;
        }
        server.send(404, "application/json", "{\"error\":\"Incident not found\"}");
        return;
    }
    size_t actualSize = 0;
    char actualSha[65] = {};
    if (!dashHashIncident(incidentPath, actualSize, actualSha))
    {
        dashSetIncidentLifecycleError("incident_hash_failed");
        server.send(500, "application/json", "{\"error\":\"Incident verification failed\"}");
        return;
    }
    if (actualSize != submittedSize || strcmp(actualSha, submittedSha) != 0)
    {
        server.send(409, "application/json", "{\"error\":\"ACK does not match incident\"}");
        return;
    }
    const bool alreadyAcknowledged = SPIFFS.exists(ackPath) &&
                                     dashAckMatches(ackPath, actualSize, actualSha);
    if (!alreadyAcknowledged && !dashWriteAck(ackPath, actualSize, actualSha))
    {
        dashSetIncidentLifecycleError("ack_persist_failed");
        server.send(500, "application/json", "{\"error\":\"ACK persistence failed\"}");
        return;
    }
    dashSetIncidentLifecycleError("");
    dashRefreshIncidentStats();
    (void)dashEnsureIncidentSpace(kDashIncidentSafetyFreeBytes);
    server.send(200, "application/json",
                alreadyAcknowledged ? "{\"ok\":true,\"alreadyAcknowledged\":true}"
                                    : "{\"ok\":true,\"alreadyAcknowledged\":false}");
}

static void handleEventDownload()
{
    if (!dashRequireRecorderAuth()) return;
    char selectedPath[sizeof(dashIncidentPath)] = {};
    uint32_t selectedSequence = 0;
    uint8_t selectedSlot = 0;
    if (server.hasArg("id"))
    {
        if (!dashIncidentIdFromText(server.arg("id"), selectedSequence, selectedSlot) ||
            !dashIncidentPathFor(selectedSequence, selectedSlot, "jsonl",
                                 selectedPath, sizeof(selectedPath)))
        {
            server.send(400, "application/json", "{\"error\":\"Invalid incident id\"}");
            return;
        }
    }
    else
    {
        bool currentIncident = false;
        bool currentPersisted = false;
        {
            DashDataGuard guard;
            currentIncident = dashRecorder.trigger() != Chassis::EventRecorder::Trigger::None;
            currentPersisted = dashIncidentPersisted && dashPersistedGeneration == dashRecorder.generation();
        }
        if (currentIncident && !currentPersisted) dashPersistFrozenIncident();
        {
            DashDataGuard guard;
            currentIncident = dashRecorder.trigger() != Chassis::EventRecorder::Trigger::None;
            currentPersisted = dashIncidentPersisted && dashPersistedGeneration == dashRecorder.generation();
        }
        if (currentIncident && !currentPersisted)
        {
            server.send(503, "text/plain", "Incident persistence is not ready");
            return;
        }
        if (!dashFindLatestIncidentPath())
        {
            server.send(409, "text/plain", "Capture is not ready");
            return;
        }
        strlcpy(selectedPath, dashIncidentPath, sizeof(selectedPath));
        (void)dashPublicationFromName(selectedPath, selectedSequence, selectedSlot);
    }
    if (SPIFFS.exists(selectedPath))
    {
        size_t expectedBytes = 0;
        char sha256[65] = {};
        if (!dashHashIncident(selectedPath, expectedBytes, sha256))
        {
            dashSetIncidentLifecycleError("incident_hash_failed");
            server.send(500, "text/plain", "Incident verification failed");
            return;
        }
        File saved = SPIFFS.open(selectedPath, "r");
        if (saved)
        {
            server.sendHeader("Cache-Control", "no-store");
            const char *filename = selectedPath[0] == '/' ? selectedPath + 1 : selectedPath;
            const String disposition = String("attachment; filename=") + filename;
            const String incidentId = String(static_cast<unsigned long>(selectedSequence)) + "-" +
                                      String(static_cast<unsigned>(selectedSlot));
            server.sendHeader("Content-Disposition", disposition.c_str());
            server.sendHeader("X-T2CAN-Incident-Id", incidentId.c_str());
            server.sendHeader("X-T2CAN-Size", String(static_cast<unsigned long>(expectedBytes)).c_str());
            server.sendHeader("X-T2CAN-SHA256", sha256);
            server.sendHeader("ETag", sha256);
            size_t sentBytes = 0;
            const bool streamOk = server.streamFile(saved, "application/x-ndjson", sentBytes);
            saved.close();
            if (!streamOk || sentBytes != expectedBytes)
            {
                dashLog(String("[ERR] Incident download failed: sent ") +
                        String((unsigned long)sentBytes) + " of " +
                        String((unsigned long)expectedBytes) + " bytes");
            }
            return;
        }
        dashLog("[ERR] Incident file exists but could not be opened for download");
    }
    server.send(404, "text/plain", "Incident not found");
}

static void dashEventTick()
{
    // Error-counter edge, not a guessed bus-off enum. Detects dual-bus errors
    // as well as single TWAI bus errors even when the browser is closed.
    static uint32_t previousErrors = 0;
    static uint8_t previousReadyMask = 0;
    char diagnostics[2048] = {};
    uint32_t errors = 0;
    uint8_t readyMask = 0;
    bool valid = false;
    if (dashDriver) {
        dashDriver->diagnosticsJson(diagnostics, sizeof(diagnostics));
        JsonDocument doc;
        if (!deserializeJson(doc, diagnostics))
        {
            valid = true;
            if (!doc["canA"].isNull()) {
                readyMask = (doc["canA"]["ready"].as<bool>() ? 1 : 0) |
                            (doc["canB"]["ready"].as<bool>() ? 2 : 0);
            } else {
                readyMask = (doc["stateCode"] | -1) == 1 ? 1 : 0;
            }
            errors = (doc["canA"]["errors"] | 0u) + (doc["canB"]["errors"] | 0u) +
                     (doc["busErrors"] | 0u) + (doc["rxErrors"] | 0u) + (doc["txErrors"] | 0u);
        }

        // Prefer the driver's physical health interface. This covers
        // MCP2515/TWAI counters even when their diagnostic JSON schemas differ.
        bool readyA = false, readyB = false, readyAny = false;
        uint32_t errorsA = 0, errorsB = 0, errorsAny = 0;
        const bool hasA = dashDriver->physicalHealth(CAN_BUS_CAN_A, readyA, errorsA);
        const bool hasB = dashDriver->physicalHealth(CAN_BUS_CAN_B, readyB, errorsB);
        if (hasA || hasB)
        {
            readyMask = (hasA && readyA ? 1 : 0) | (hasB && readyB ? 2 : 0);
            errors = (hasA ? errorsA : 0) + (hasB ? errorsB : 0);
            valid = true;
        }
        else if (dashDriver->physicalHealth(CAN_BUS_ANY, readyAny, errorsAny))
        {
            readyMask = readyAny ? 1 : 0;
            errors = errorsAny;
            valid = true;
        }
    }
    bool shouldPersist = false;
    {
        // CAN processing holds AppHandlerGuard before invoking dashboard
        // observers. Take it first here as well, then DashDataGuard, so a
        // handler's automatic profile transition cannot race recorder freeze.
        AppHandlerGuard appGuard;
        DashDataGuard guard;
        const uint32_t now = millis();
        if (valid) {
            if (errors > previousErrors || (previousReadyMask & ~readyMask) != 0)
                dashRecorder.mark(Chassis::EventRecorder::Trigger::CanError, now);
            previousErrors = errors;
            previousReadyMask = readyMask;
        }
        if (dashCanSeen && Chassis::EventRecorder::postDeadlineReached(now, lastFrameMs) &&
            dashRecorder.enabled() && dashRecorder.trigger() == Chassis::EventRecorder::Trigger::None &&
            dashCanLossGeneration != dashRecorder.generation())
        {
            if (dashRecorder.mark(Chassis::EventRecorder::Trigger::CanLoss, now))
                dashCanLossGeneration = dashRecorder.generation();
        }
        dashRecorder.recordCanLiveness(
            dashCanSeen && !Chassis::EventRecorder::postDeadlineReached(now, lastFrameMs), now);
        dashRecorder.tick(now);
        shouldPersist = dashRecorder.frozen() &&
                        !(dashIncidentPersisted && dashPersistedGeneration == dashRecorder.generation());
    }
    mcpDashRecordCanHealth();
    if (shouldPersist)
        dashPersistFrozenIncident();
}

static void handleStatus()
{
    unsigned long now = 0;
    bool canOnlineSnapshot = false;
    bool hardwareReadySnapshot = false;
    bool vehicleOnlineSnapshot = false;
    DashWriteProbe writeProbeSnapshot = {};
    Chassis::TelemetrySnapshot telemetrySnapshot = {};
    Chassis::LayoutRecommendation::Evidence layoutEvidence = {};
    Chassis::LayoutRecommendation::Result layoutRecommendation = {};
    CanAnomaly::Summary anomalySummary = {};
    bool recorderArmed = false, recorderFrozen = false, recorderUsingPsram = false;
    size_t recorderRawCount = 0, recorderStateCount = 0, recorderRawCapacity = 0, recorderStateCapacity = 0;
    uint32_t recorderCoverage = 0, recorderStateCoverage = 0, recorderDrops = 0;
    uint32_t recorderRawDrops = 0, recorderStateDrops = 0, recorderStateProtectedDrops = 0, recorderGeneration = 0;
    uint32_t incidentEvictions = 0;
    uint16_t pendingIncidents = 0, acknowledgedIncidents = 0;
    size_t incidentStorageTotal = 0, incidentStorageFree = 0;
    bool recorderPersisted = false, recorderPersistFailure = false, recorderStateTargetReady = false;
    bool incidentStoragePressure = false;
    char incidentLastError[sizeof(dashIncidentLastError)] = {};
    {
        DashDataGuard guard;
        now = millis();
        if (canOnline && Chassis::EventRecorder::postDeadlineReached(now, lastFrameMs))
            canOnline = false;
        canOnlineSnapshot = canOnline;
        hardwareReadySnapshot = dashDriver && dashDriver->ready();
        vehicleOnlineSnapshot = dashVehicleCanSeen &&
                                !Chassis::EventRecorder::postDeadlineReached(now, dashLastVehicleFrameMs);
        writeProbeSnapshot = dashWriteProbe;
        telemetrySnapshot = dashTelemetry.snapshot(now);
        layoutEvidence = dashLayoutTracker.evidence(
            now, 1500,
            static_cast<uint8_t>(dashDasLayoutOverride) == 2,
            static_cast<uint8_t>(dashDasLayoutOverride) == 3);
        layoutRecommendation = Chassis::LayoutRecommendation::recommend(layoutEvidence);
        dashAnomalyTracker.tick(now);
        anomalySummary = dashAnomalyTracker.summary();
        recorderArmed = dashRecorder.enabled();
        recorderFrozen = dashRecorder.frozen();
        recorderRawCount = dashRecorder.rawCount();
        recorderStateCount = dashRecorder.stateCount();
        recorderRawCapacity = dashRecorder.rawCapacity();
        recorderStateCapacity = dashRecorder.stateCapacity();
        recorderCoverage = dashRecorder.coverageMs();
        recorderStateCoverage = dashRecorder.stateHistoryCoverageMs();
        recorderStateTargetReady = dashRecorder.stateTargetWindowReady();
        recorderDrops = dashRecorder.drops();
        recorderRawDrops = dashRecorder.rawDrops();
        recorderStateDrops = dashRecorder.stateDrops();
        recorderStateProtectedDrops = dashRecorder.stateProtectedDrops();
        recorderUsingPsram = dashRecorder.usingPsram();
        recorderGeneration = dashRecorder.generation();
        recorderPersisted = dashIncidentPersisted && dashPersistedGeneration == recorderGeneration;
        recorderPersistFailure = dashIncidentPersistFailure && dashPersistFailureGeneration == recorderGeneration;
        pendingIncidents = dashIncidentPendingCount;
        acknowledgedIncidents = dashIncidentAcknowledgedCount;
        incidentStorageTotal = dashIncidentStorageTotalBytes;
        incidentStorageFree = dashIncidentStorageFreeBytes;
        incidentStoragePressure = dashIncidentStoragePressure;
        incidentEvictions = dashIncidentEvictions;
        strlcpy(incidentLastError, dashIncidentLastError, sizeof(incidentLastError));
    }

    char driverJson[768] = "{\"type\":\"unavailable\",\"stateCode\":0}";
    if (dashDriver)
        dashDriver->diagnosticsJson(driverJson, sizeof(driverJson));

    const bool injectionActive = dashInjectionActive();
    const DashApGateSnapshot apGate = dashApGateSnapshot();
    char response[4096];
    BoundedTextWriter json(response, sizeof(response));
    json.appendf("{\"can\":%s,\"ia\":%s,\"ready\":%s,\"hardwareReady\":%s,\"trafficSeen\":%s,\"vehicleOnline\":%s",
                 canOnlineSnapshot ? "true" : "false",
                 injectionActive ? "true" : "false",
                 appInjectionReady() ? "true" : "false",
                 hardwareReadySnapshot ? "true" : "false",
                 canOnlineSnapshot ? "true" : "false",
                 vehicleOnlineSnapshot ? "true" : "false");
    json.appendf(
        ",\"apGate\":{\"enabled\":%s,\"allowed\":%s,\"ap\":%s,\"parked\":%s,"
        "\"summoning\":%s,\"stableMs\":%lu,\"reason\":\"%s\"}",
        apGate.enabled ? "true" : "false", apGate.allowed ? "true" : "false",
        apGate.apActive ? "true" : "false", apGate.parked ? "true" : "false",
        apGate.summoning ? "true" : "false", apGate.stableMs, apGate.reason);
    json.appendf(
        ",\"layoutRecommendation\":{\"candidate\":\"%s\",\"confidence\":\"%s\","
        "\"ambiguous\":%s,\"requiresConfirmation\":true,\"automaticMutation\":false,"
        "\"evidence\":{\"legacy399Valid\":%lu,\"hw439bValid\":%lu,\"rejectedDlc\":%lu,"
        "\"legacyFresh\":%s,\"hw4Fresh\":%s},\"alternatives\":[",
        Chassis::LayoutRecommendation::layoutName(layoutRecommendation.candidate),
        Chassis::LayoutRecommendation::confidenceName(layoutRecommendation.confidence),
        layoutRecommendation.ambiguous ? "true" : "false",
        static_cast<unsigned long>(layoutEvidence.legacy399Valid),
        static_cast<unsigned long>(layoutEvidence.hw4_39bValid),
        static_cast<unsigned long>(layoutEvidence.rejectedDlc),
        layoutEvidence.legacyFresh ? "true" : "false",
        layoutEvidence.hw4Fresh ? "true" : "false");
    for (uint8_t i = 0; i < layoutRecommendation.alternativeCount; i++)
    {
        if (i > 0) json.append(",");
        json.appendf("\"%s\"", Chassis::LayoutRecommendation::layoutName(
                                   layoutRecommendation.alternatives[i]));
    }
    json.append("]}");
    json.appendf(
        ",\"anomaly\":{\"schema\":\"t2can-can-anomaly-v1\",\"flags\":%u,"
        "\"events\":%lu,\"frames\":%lu,\"newIds\":%lu,\"dlcChanges\":%lu,"
        "\"periodChanges\":%lu,\"bursts\":%lu,\"stalls\":%lu,\"recoveries\":%lu,"
        "\"asymmetries\":%lu,\"echoMismatches\":%lu,\"capacityDrops\":%lu,"
        "\"lastEventMs\":%lu,\"lastId\":%lu,\"lastPhysicalBus\":%u,"
        "\"policyBlock\":%s,\"automaticMutation\":false}",
        static_cast<unsigned>(anomalySummary.flags),
        static_cast<unsigned long>(anomalySummary.events),
        static_cast<unsigned long>(anomalySummary.frames),
        static_cast<unsigned long>(anomalySummary.newIds),
        static_cast<unsigned long>(anomalySummary.dlcChanges),
        static_cast<unsigned long>(anomalySummary.periodChanges),
        static_cast<unsigned long>(anomalySummary.bursts),
        static_cast<unsigned long>(anomalySummary.stalls),
        static_cast<unsigned long>(anomalySummary.recoveries),
        static_cast<unsigned long>(anomalySummary.asymmetries),
        static_cast<unsigned long>(anomalySummary.echoMismatches),
        static_cast<unsigned long>(anomalySummary.capacityDrops),
        static_cast<unsigned long>(anomalySummary.lastEventMs),
        static_cast<unsigned long>(anomalySummary.lastId),
        static_cast<unsigned>(anomalySummary.lastPhysicalBus),
        anomalySummary.policyBlock ? "true" : "false");
#ifdef ESP_PLATFORM
    json.appendf(
        ",\"runtime\":{\"uptimeMs\":%lu,\"canFrames\":%lu,\"canAgeMs\":%lu,"
        "\"txOk\":%lu,\"txFail\":%lu,\"freeHeap\":%lu,\"delayRemainingMs\":%lu,"
        "\"frameThreshold\":%lu}",
        now,
        static_cast<unsigned long>(RuntimeDiagnostics::canFrames.load(std::memory_order_relaxed)),
        static_cast<unsigned long>(RuntimeDiagnostics::canAgeMs(now)),
        static_cast<unsigned long>(RuntimeDiagnostics::txOk.load(std::memory_order_relaxed)),
        static_cast<unsigned long>(RuntimeDiagnostics::txFail.load(std::memory_order_relaxed)),
        static_cast<unsigned long>(esp_get_free_heap_size()),
        static_cast<unsigned long>(RuntimeDiagnostics::injectionDelayRemainingMs(now)),
        static_cast<unsigned long>(CAN_LIVE_FRAME_THRESHOLD));
    json.appendf(
        ",\"otaBoot\":{\"state\":\"%s\",\"pending\":%s,\"preflightPassed\":%s,"
        "\"confirmRemainingMs\":%lu,\"lastError\":%ld}",
        RuntimeDiagnostics::otaBootStateName(),
        RuntimeDiagnostics::otaBootState.load(std::memory_order_relaxed) ==
                RuntimeDiagnostics::OtaBootState::Pending
            ? "true"
            : "false",
        RuntimeDiagnostics::otaPreflightPassed.load(std::memory_order_relaxed) ? "true" : "false",
        static_cast<unsigned long>(RuntimeDiagnostics::otaConfirmRemainingMs.load(std::memory_order_relaxed)),
        static_cast<long>(RuntimeDiagnostics::otaLastError.load(std::memory_order_relaxed)));
    const uint8_t manifestConfig[] = {
        static_cast<uint8_t>(hwMode), static_cast<uint8_t>(canActive ? 1 : 0),
        static_cast<uint8_t>(apInjectionGate ? 1 : 0),
        static_cast<uint8_t>(summonOnlyInjection ? 1 : 0),
        static_cast<uint8_t>(dashDasLayoutOverride),
        static_cast<uint8_t>(dashSpeedProfileAuto ? 1 : 0),
        static_cast<uint8_t>(dashManualSpeedProfile), static_cast<uint8_t>(dashNagMode)};
    uint32_t manifestDigest = 2166136261u;
    for (uint8_t value : manifestConfig)
    {
        manifestDigest ^= value;
        manifestDigest *= 16777619u;
    }
    const auto manifestOtaState = RuntimeDiagnostics::otaBootState.load(std::memory_order_relaxed);
    const char *manifestSelfTest = RuntimeDiagnostics::otaPreflightPassed.load(std::memory_order_relaxed)
                                       ? "passed"
                                       : manifestOtaState == RuntimeDiagnostics::OtaBootState::Rollback
                                             ? "failed"
                                             : "not_run";
    const char *manifestTxMode = appMaintenanceTxInhibit ? "maintenance"
                                 : !canActive             ? "disabled"
                                 : !appInjectionReady()   ? "blocked"
                                                         : "active";
#if defined(DRIVER_T2CAN_DUAL)
    const char *manifestPhysicalBuses = "[\"canA\",\"canB\"]";
    const char *manifestFeatures =
        "[\"dashboard\",\"ota\",\"dual-can\",\"recorder\",\"self-test\"]";
#else
    const char *manifestPhysicalBuses = "[\"can\"]";
    const char *manifestFeatures =
        "[\"dashboard\",\"ota\",\"single-can\",\"recorder\",\"self-test\"]";
#endif
    json.appendf(
        ",\"manifest\":{\"schema\":\"t2can-firmware-manifest-v1\","
        "\"firmwareVersion\":\"%s\",\"gitRevision\":\"%s\","
        "\"buildEnvironment\":\"%s\",\"boardProfile\":\"%s\","
        "\"physicalBuses\":%s,\"semanticBuses\":[\"party\",\"vehicle\"],"
        "\"features\":%s,\"decoderSchema\":\"%s\",\"decoderEntries\":%u,"
        "\"txPolicy\":{\"version\":\"t2can-tx-policy-v1\",\"effectiveMode\":\"%s\"},"
        "\"ota\":{\"artifact\":\"%s\",\"state\":\"%s\"},"
        "\"selfTest\":\"%s\",\"configDigest\":\"%08lx\"}",
        FIRMWARE_VERSION, FIRMWARE_GIT_REV, FIRMWARE_BUILD_ENV, FIRMWARE_BUILD_ENV,
        manifestPhysicalBuses, manifestFeatures, Chassis::DecoderRegistry::kSchemaVersion,
        static_cast<unsigned>(Chassis::DecoderRegistry::size()), manifestTxMode,
        FIRMWARE_ARTIFACT, RuntimeDiagnostics::otaBootStateName(), manifestSelfTest,
        static_cast<unsigned long>(manifestDigest));
#endif
    json.appendf(
        ",\"telemetry\":{\"accepted\":%lu,\"lastMs\":%lu,"
        "\"speed\":{\"seen\":%s,\"kph\":%.2f,\"display\":%u,\"ageMs\":%lu},"
        "\"gear\":{\"seen\":%s,\"value\":%u,\"autonomy\":%s,\"ageMs\":%lu},"
        "\"steering\":{\"seen\":%s,\"deg\":%.2f,\"ageMs\":%lu},"
        "\"brake\":{\"seen\":%s,\"applied\":%s,\"ageMs\":%lu},"
        "\"das\":{\"seen\":%s,\"ap\":%u,\"handsOn\":%u,\"ageMs\":%lu,\"sideCollisionAvoid\":%u,\"laneDepartureWarning\":%u},"
        "\"acc\":{\"seen\":%s,\"report\":%u,\"activationFailure\":%u,\"ageMs\":%lu},"
        "\"dasSettings\":{\"seen\":%s,\"autosteerEnabled\":%s,\"ageMs\":%lu},"
        "\"diModes\":{\"seen\":%s,\"track\":%u,\"traction\":%u,\"ageMs\":%lu},"
        "\"presence\":{\"apLegacy\":%s,\"apControl\":%s,\"dasSteering\":%s,\"map\":%s},"
        "\"party\":{\"bmsHv\":%s,\"bmsSoc\":%s,\"bmsThermal\":%s,\"energy\":%s,\"torque\":%s,\"diState\":%s,\"warning\":%s},"
        "\"tier\":{\"seen\":%s,\"value\":%u,\"ageMs\":%lu}}",
        static_cast<unsigned long>(telemetrySnapshot.acceptedFrames),
        static_cast<unsigned long>(telemetrySnapshot.lastObservedMs),
        telemetrySnapshot.speedSeen ? "true" : "false",
        telemetrySnapshot.speedKph, static_cast<unsigned int>(telemetrySnapshot.displaySpeed),
        telemetrySnapshot.speedSeen ? now - telemetrySnapshot.speedMs : 0UL,
        telemetrySnapshot.gearSeen ? "true" : "false",
        static_cast<unsigned int>(telemetrySnapshot.gear),
        telemetrySnapshot.autonomyActive ? "true" : "false",
        telemetrySnapshot.gearSeen ? now - telemetrySnapshot.gearMs : 0UL,
        telemetrySnapshot.steeringSeen ? "true" : "false",
        telemetrySnapshot.steeringAngleDeg,
        telemetrySnapshot.steeringSeen ? now - telemetrySnapshot.steeringMs : 0UL,
        telemetrySnapshot.brakeSeen ? "true" : "false",
        telemetrySnapshot.brakeApplied ? "true" : "false",
        telemetrySnapshot.brakeSeen ? now - telemetrySnapshot.brakeMs : 0UL,
        telemetrySnapshot.dasSeen ? "true" : "false",
        static_cast<unsigned int>(telemetrySnapshot.apState),
        static_cast<unsigned int>(telemetrySnapshot.handsOn),
        telemetrySnapshot.dasSeen ? now - telemetrySnapshot.dasMs : 0UL,
        static_cast<unsigned int>(telemetrySnapshot.sideCollisionAvoid),
        static_cast<unsigned int>(telemetrySnapshot.laneDepartureWarning),
        telemetrySnapshot.dasStatus2Seen ? "true" : "false",
        static_cast<unsigned int>(telemetrySnapshot.accReport),
        static_cast<unsigned int>(telemetrySnapshot.activationFailureStatus),
        telemetrySnapshot.dasStatus2Seen ? now - telemetrySnapshot.dasStatus2Ms : 0UL,
        telemetrySnapshot.dasSettingsSeen ? "true" : "false",
        telemetrySnapshot.autosteerEnabled ? "true" : "false",
        telemetrySnapshot.dasSettingsSeen ? now - telemetrySnapshot.dasSettingsMs : 0UL,
        telemetrySnapshot.diModesSeen ? "true" : "false",
        static_cast<unsigned int>(telemetrySnapshot.trackModeState),
        static_cast<unsigned int>(telemetrySnapshot.tractionControlMode),
        telemetrySnapshot.diModesSeen ? now - telemetrySnapshot.diModesMs : 0UL,
        telemetrySnapshot.apLegacySeen ? "true" : "false",
        telemetrySnapshot.apControlSeen ? "true" : "false",
        telemetrySnapshot.dasSteeringSeen ? "true" : "false",
        telemetrySnapshot.mapSeen ? "true" : "false",
        telemetrySnapshot.bmsHvSeen ? "true" : "false",
        telemetrySnapshot.bmsSocSeen ? "true" : "false",
        telemetrySnapshot.bmsThermalSeen ? "true" : "false",
        telemetrySnapshot.energySeen ? "true" : "false",
        telemetrySnapshot.torqueSeen ? "true" : "false",
        telemetrySnapshot.diStateSeen ? "true" : "false",
        telemetrySnapshot.warningSeen ? "true" : "false",
        telemetrySnapshot.tierSeen ? "true" : "false",
        static_cast<unsigned int>(telemetrySnapshot.tier),
        telemetrySnapshot.tierSeen ? now - telemetrySnapshot.tierMs : 0UL);
    json.appendf(",\"recorder\":{\"armed\":%s,\"frozen\":%s,\"rawCount\":%u,\"stateCount\":%u,\"rawCapacity\":%u,\"stateCapacity\":%u,\"coverageMs\":%lu,\"stateCoverageMs\":%lu,\"stateTargetMs\":%lu,\"stateTargetReady\":%s,\"drops\":%lu,\"rawDrops\":%lu,\"stateDrops\":%lu,\"stateProtectedDrops\":%lu,\"usingPsram\":%s,\"generation\":%lu,\"persisted\":%s,\"persistFailure\":%s},\"incidentStore\":{\"pending\":%u,\"acknowledged\":%u,\"totalBytes\":%lu,\"freeBytes\":%lu,\"pressure\":%s,\"evictions\":%lu,\"lastError\":\"%s\"},\"psram\":{\"verified\":%s,\"bytes\":%lu,\"probeBytes\":%lu}",
                       recorderArmed ? "true" : "false", recorderFrozen ? "true" : "false",
                       static_cast<unsigned>(recorderRawCount), static_cast<unsigned>(recorderStateCount),
                       static_cast<unsigned>(recorderRawCapacity), static_cast<unsigned>(recorderStateCapacity),
                       static_cast<unsigned long>(recorderCoverage), static_cast<unsigned long>(recorderStateCoverage),
                       static_cast<unsigned long>(Chassis::EventRecorder::StateTargetWindowMs),
                       recorderStateTargetReady ? "true" : "false", static_cast<unsigned long>(recorderDrops),
                       static_cast<unsigned long>(recorderRawDrops), static_cast<unsigned long>(recorderStateDrops),
                       static_cast<unsigned long>(recorderStateProtectedDrops),
                       recorderUsingPsram ? "true" : "false", static_cast<unsigned long>(recorderGeneration),
                       recorderPersisted ? "true" : "false", recorderPersistFailure ? "true" : "false",
                       static_cast<unsigned>(pendingIncidents), static_cast<unsigned>(acknowledgedIncidents),
                       static_cast<unsigned long>(incidentStorageTotal), static_cast<unsigned long>(incidentStorageFree),
                       incidentStoragePressure ? "true" : "false", static_cast<unsigned long>(incidentEvictions),
                       incidentLastError,
        #ifdef ESP_PLATFORM
                       RuntimeDiagnostics::psramVerified.load(std::memory_order_relaxed) ? "true" : "false",
                       static_cast<unsigned long>(RuntimeDiagnostics::systemInfo.psramBytes),
                       static_cast<unsigned long>(RuntimeDiagnostics::psramProbeBytes.load(std::memory_order_relaxed))
        #else
                       "false", 0UL, 0UL
        #endif
        );
        json.appendf(",\"driver\":%s,\"probe\":{\"active\":%s,\"state\":%u,\"id\":%lu,"
                     "\"mux\":%d,\"txa\":%lu,\"rxa\":%lu,\"txdlc\":%u,\"rxdlc\":%u,"
                     "\"hasrx\":%s,\"tx\":[",
                     driverJson, writeProbeSnapshot.active ? "true" : "false",
                 static_cast<unsigned int>(writeProbeSnapshot.state),
                 static_cast<unsigned long>(writeProbeSnapshot.id),
                 static_cast<int>(writeProbeSnapshot.mux),
                 writeProbeSnapshot.active ? now - writeProbeSnapshot.txMs : 0,
                 writeProbeSnapshot.hasRx ? now - writeProbeSnapshot.rxMs : 0,
                 static_cast<unsigned int>(writeProbeSnapshot.txDlc),
                 static_cast<unsigned int>(writeProbeSnapshot.rxDlc),
                 writeProbeSnapshot.hasRx ? "true" : "false");
    for (uint8_t i = 0; i < writeProbeSnapshot.txDlc; i++)
    {
        if (i)
            json.append(",");
        json.appendf("%u", static_cast<unsigned int>(writeProbeSnapshot.txData[i]));
    }
    json.append("],\"rx\":[");
    for (uint8_t i = 0; i < writeProbeSnapshot.rxDlc; i++)
    {
        if (i)
            json.append(",");
        json.appendf("%u", static_cast<unsigned int>(writeProbeSnapshot.rxData[i]));
    }
    json.append("]}}");
    if (!json.complete())
    {
        server.send(500, "application/json", "{\"error\":\"Status response overflow\"}");
        return;
    }
#ifdef ESP_PLATFORM
    RuntimeDiagnostics::lastStatusBytes.store(static_cast<uint32_t>(json.size()),
                                              std::memory_order_relaxed);
#endif
    dashSendBuffer(200, "application/json", json.data(), json.size());
}

#ifdef ESP_PLATFORM
static const char *dashWriteProbeStateName(uint8_t state)
{
    switch (state)
    {
    case kDashWriteProbePending:
        return "waiting for matching frame";
    case kDashWriteProbeMatch:
        return "matching frame seen";
    case kDashWriteProbeDifferent:
        return "bus frame differs from write";
    case kDashWriteProbeFailed:
        return "transmit failed";
    default:
        return "no write recorded";
    }
}

static void dashAppendFrameBytes(BoundedTextWriter &report, const uint8_t *data, uint8_t dlc)
{
    if (!data || dlc == 0)
    {
        report.append("unavailable");
        return;
    }
    for (uint8_t i = 0; i < dlc && i < 8; i++)
        report.appendf("%s%02X", i ? " " : "", static_cast<unsigned int>(data[i]));
}

static void dashAppendStackWatermark(BoundedTextWriter &report, const char *label,
                                     TaskHandle_t taskHandle)
{
    if (!taskHandle)
    {
        report.appendf("%s: unavailable\n", label);
        return;
    }
    report.appendf("%s: %lu bytes (ESP-IDF high-water mark)\n", label,
                   static_cast<unsigned long>(uxTaskGetStackHighWaterMark(taskHandle)));
}

static void handleSupport()
{
    RuntimeDiagnostics::supportRequests.fetch_add(1, std::memory_order_relaxed);
    const uint32_t now = millis();
    const bool injectionActive = dashInjectionActive();
    const DashApGateSnapshot apGate = dashApGateSnapshot();
    bool canOnlineSnapshot = false;
    DashWriteProbe writeProbeSnapshot = {};
    {
        DashDataGuard guard;
        canOnlineSnapshot = canOnline && !Chassis::EventRecorder::postDeadlineReached(now, lastFrameMs);
        writeProbeSnapshot = dashWriteProbe;
    }

    char driverSummary[768] = "CAN driver diagnostics unavailable";
    char canConfiguration[128] = "bitrate=500000 pins=unavailable";
    if (dashDriver)
    {
        dashDriver->diagnosticsSummary(driverSummary, sizeof(driverSummary));
        dashDriver->configurationSummary(canConfiguration, sizeof(canConfiguration));
    }

    wifi_ap_record_t wifiRecord = {};
    const bool wifiConnected = dashStaConnectedSnapshot() &&
                               esp_wifi_sta_get_ap_info(&wifiRecord) == ESP_OK;
    char wifiSsid[33] = {};
    if (wifiConnected)
        memcpy(wifiSsid, wifiRecord.ssid, sizeof(wifiSsid) - 1);
    else
    {
        DashWifiGuard guard;
        strlcpy(wifiSsid, staSSID, sizeof(wifiSsid));
    }
    String ipAddress = wifiConnected ? WiFi.localIP().toString() : String("unavailable");
    uint8_t mac[6] = {};
    const bool hasMac = esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK;

    uint32_t configuredIds[160] = {};
    const uint8_t configuredIdCount =
        dashCollectMergedFilterIds(configuredIds, sizeof(configuredIds) / sizeof(configuredIds[0]));
    struct SupportPluginSummary
    {
        char name[PLUGIN_NAME_MAX];
        uint8_t priority;
        uint8_t ruleCount;
    };
    SupportPluginSummary enabledPlugins[PLUGIN_MAX] = {};
    uint8_t enabledPluginCount = 0;
    {
        PluginLockGuard guard;
        for (uint8_t i = 0; i < pluginCount && enabledPluginCount < PLUGIN_MAX; i++)
        {
            if (!pluginStore[i].enabled)
                continue;
            SupportPluginSummary &summary = enabledPlugins[enabledPluginCount++];
            strlcpy(summary.name, pluginStore[i].name, sizeof(summary.name));
            summary.priority = pluginStore[i].priority;
            summary.ruleCount = pluginStore[i].ruleCount;
        }
    }

    const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largestInternal =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const uint32_t minimumHeap = esp_get_minimum_free_heap_size();
    const uint32_t txFailures = RuntimeDiagnostics::txFail.load(std::memory_order_relaxed);
    const bool nvsError = RuntimeDiagnostics::nvsState.load(std::memory_order_relaxed) ==
                          RuntimeDiagnostics::NvsState::Error;
    const bool busOff = strstr(driverSummary, "state=bus_off") != nullptr;
    const bool lowHeap = minimumHeap < 32768 || largestInternal < 16384;
    const bool warning = !canOnlineSnapshot || !appInjectionReady() || txFailures > 0 || lowHeap ||
                         RuntimeDiagnostics::nvsState.load(std::memory_order_relaxed) ==
                             RuntimeDiagnostics::NvsState::Recovered;
    const char *overall = nvsError || busOff ? "ERROR" : warning ? "WARNING"
                                                                 : "OK";

    char response[6144];
    BoundedTextWriter report(response, sizeof(response));
    report.append("ev-open-can-tools support report\n");
    report.append("Report schema: 2\n");
    report.appendf("Overall: %s\n", overall);
    report.appendf("Captured uptime: %lu ms\n\n", static_cast<unsigned long>(now));

    report.append("[Firmware]\n");
    report.appendf("Name: ev-open-can-tools\nVersion: %s\n", FIRMWARE_VERSION);
    report.appendf("Build date: %s %s\n", __DATE__, __TIME__);
#if defined(CONFIG_COMPILER_OPTIMIZATION_DEBUG)
    report.append("Build type: debug\n");
#elif defined(CONFIG_COMPILER_OPTIMIZATION_SIZE)
    report.append("Build type: release (size optimized)\n");
#else
    report.append("Build type: release\n");
#endif
    report.appendf("ESP-IDF: %s\nTarget: %s\nChip revision: %u\nCPU cores: %u\n",
                   esp_get_idf_version(), CONFIG_IDF_TARGET,
                   static_cast<unsigned int>(RuntimeDiagnostics::systemInfo.chip.revision),
                   static_cast<unsigned int>(RuntimeDiagnostics::systemInfo.chip.cores));
    report.appendf("Flash size: %lu bytes\nReset reason: %d (%s)\nRTC boot count: %lu\n\n",
                   static_cast<unsigned long>(RuntimeDiagnostics::systemInfo.flashBytes),
                   static_cast<int>(RuntimeDiagnostics::bootResetReason),
                   RuntimeDiagnostics::resetReasonName(RuntimeDiagnostics::bootResetReason),
                   static_cast<unsigned long>(RuntimeDiagnostics::rtcBootCount));

    report.append("[Memory]\n");
    report.appendf("Health: %s\nFree heap: %lu bytes\nMinimum free heap: %lu bytes\n",
                   lowHeap ? "[WARN] low-water threshold crossed" : "[OK]",
                   static_cast<unsigned long>(esp_get_free_heap_size()),
                   static_cast<unsigned long>(minimumHeap));
    report.appendf("Largest internal block: %lu bytes\nInternal RAM: %lu free / %lu total bytes\n",
                   static_cast<unsigned long>(largestInternal),
                   static_cast<unsigned long>(freeInternal),
                   static_cast<unsigned long>(RuntimeDiagnostics::systemInfo.internalRamBytes));
    if (RuntimeDiagnostics::systemInfo.psramBytes)
        report.appendf("PSRAM: %lu free / %lu total bytes\n",
                       static_cast<unsigned long>(freePsram),
                       static_cast<unsigned long>(RuntimeDiagnostics::systemInfo.psramBytes));
    else
        report.append("PSRAM: unavailable\n");

    report.append("\n[Tasks]\n");
    dashAppendStackWatermark(report, "Main/CAN task", RuntimeDiagnostics::mainTaskHandle);
    dashAppendStackWatermark(report, "Web maintenance task", webTaskHandle);
    dashAppendStackWatermark(report, "HTTP server task", xTaskGetCurrentTaskHandle());
    dashAppendStackWatermark(report, "GVRET task", GvretSerial::taskHandle);
    report.appendf("Main loop wakes: %lu\nCAN loop wakes: %lu\nCAN RX heartbeat: %lu\n",
                   static_cast<unsigned long>(RuntimeDiagnostics::mainHeartbeat.load(std::memory_order_relaxed)),
                   static_cast<unsigned long>(RuntimeDiagnostics::canHeartbeat.load(std::memory_order_relaxed)),
                   static_cast<unsigned long>(RuntimeDiagnostics::canRxHeartbeat.load(std::memory_order_relaxed)));
    report.appendf("Web maintenance wakes: %lu\nHeartbeat reports: %lu\nNo-CAN warnings: %lu\n",
                   static_cast<unsigned long>(RuntimeDiagnostics::webHeartbeat.load(std::memory_order_relaxed)),
                   static_cast<unsigned long>(RuntimeDiagnostics::heartbeatLogs.load(std::memory_order_relaxed)),
                   static_cast<unsigned long>(RuntimeDiagnostics::noCanWarnings.load(std::memory_order_relaxed)));
    if (now > 0)
        report.appendf(
            "Average wake rates: main=%llu/s can=%llu/s web=%llu/s\nAverage CAN RX rate: %llu frames/s\n",
            static_cast<unsigned long long>(RuntimeDiagnostics::mainHeartbeat.load(std::memory_order_relaxed)) *
                1000ULL / now,
            static_cast<unsigned long long>(RuntimeDiagnostics::canHeartbeat.load(std::memory_order_relaxed)) *
                1000ULL / now,
            static_cast<unsigned long long>(RuntimeDiagnostics::webHeartbeat.load(std::memory_order_relaxed)) *
                1000ULL / now,
            static_cast<unsigned long long>(RuntimeDiagnostics::canFrames.load(std::memory_order_relaxed)) *
                1000ULL / now);

    report.append("\n[WiFi]\n");
    report.appendf("State: %s\nSSID: ", wifiConnected ? "connected" : "disconnected");
    report.appendSafe(wifiSsid[0] ? wifiSsid : "unavailable", sizeof(wifiSsid) - 1);
    report.appendf("\nIP address: %s\nRSSI: ", ipAddress.c_str());
    if (wifiConnected)
        report.appendf("%d dBm\n", static_cast<int>(wifiRecord.rssi));
    else
        report.append("unavailable\n");
    if (hasMac)
        report.appendf("MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2],
                       mac[3], mac[4], mac[5]);
    else
        report.append("MAC address: unavailable\n");
    report.appendf("AP clients: %d\n", WiFi.softAPgetStationNum());

    report.append("\n[Configuration]\n");
    report.appendf("Hardware mode: %s\nNag suppression: %s\nSpeed profile: %u (%s)\nPlugin replay: %u\n",
                   hwMode == 0 ? "Legacy" : hwMode == 1 ? "HW3"
                                                        : "HW4",
                   nagModeName(dashNagMode),
                   static_cast<unsigned int>(dashManualSpeedProfile),
                   dashSpeedProfileAuto ? "automatic" : "manual",
                   static_cast<unsigned int>(pluginGetReplayCount()));
    report.appendf("AP injection gate: %s\nSummon-only injection (beta): %s\nOffset slew: %s",
                   apInjectionGate ? "enabled" : "disabled",
                   summonOnlyInjection ? "enabled" : "disabled",
                   hw3OffsetSlew ? "enabled" : "disabled");
    if (hw3OffsetSlew)
        report.appendf(" at %u%%/s", static_cast<unsigned int>(hw3SlewRate));
    report.appendf("\nUpdate channel: %s\nAutomatic update: %s\nEnabled plugins (%u): ",
                   updateBetaChannel ? "beta" : "stable",
                   autoUpdateEnabled ? "enabled" : "disabled",
                   static_cast<unsigned int>(enabledPluginCount));
    if (!enabledPluginCount)
        report.append("none");
    for (uint8_t i = 0; i < enabledPluginCount; i++)
    {
        report.appendf("%s#%u ", i ? ", " : "", static_cast<unsigned int>(enabledPlugins[i].priority));
        report.appendSafe(enabledPlugins[i].name, sizeof(enabledPlugins[i].name) - 1);
        report.appendf(" (%u rules)", static_cast<unsigned int>(enabledPlugins[i].ruleCount));
    }
    report.append("\n");

    report.append("\n[CAN]\n");
    report.appendf("Health: %s\nState: %s\nConfiguration: %s\nDriver: %s\n",
                   busOff ? "[ERROR] bus-off" : canOnlineSnapshot ? "[OK]"
                                                                  : "[WARN] offline",
                   canOnlineSnapshot ? "online" : "offline", canConfiguration, driverSummary);
    report.appendf("Total received frames: %lu\nLast frame age: ",
                   static_cast<unsigned long>(RuntimeDiagnostics::canFrames.load(std::memory_order_relaxed)));
    const uint32_t canAge = RuntimeDiagnostics::canAgeMs(now);
    if (canAge == UINT32_MAX)
        report.append("unavailable\n");
    else
        report.appendf("%lu ms\n", static_cast<unsigned long>(canAge));
    report.appendf("TX successful: %lu\nTX failed: %lu\n",
                   static_cast<unsigned long>(RuntimeDiagnostics::txOk.load(std::memory_order_relaxed)),
                   static_cast<unsigned long>(txFailures));
    report.appendf("Active firmware mode: %s\nConfigured CAN IDs (%u): ",
                   hwMode == 0 ? "Legacy" : hwMode == 1 ? "HW3"
                                                        : "HW4",
                   static_cast<unsigned int>(configuredIdCount));
    if (!configuredIdCount)
        report.append("none");
    for (uint8_t i = 0; i < configuredIdCount; i++)
        report.appendf("%s0x%03lX", i ? "," : "", static_cast<unsigned long>(configuredIds[i]));
    report.appendf("\nDriver-wake delay: %lu ms\nPost-CAN injection delay: %lu ms\n",
                   DRIVER_WAKE_DELAY_MS, INJECTION_DELAY_MS);
    report.appendf("CAN-seen gate: %s (%lu frames, requires >%lu)\n",
                   RuntimeDiagnostics::canFrames.load(std::memory_order_relaxed) > CAN_LIVE_FRAME_THRESHOLD
                       ? "passed"
                       : "blocked",
                   static_cast<unsigned long>(RuntimeDiagnostics::canFrames.load(std::memory_order_relaxed)),
                   static_cast<unsigned long>(CAN_LIVE_FRAME_THRESHOLD));
    report.appendf("Injection configured: %s\nInjection state: %s\n",
                   canActive ? "armed" : "stopped", injectionActive ? "enabled" : "blocked");
    report.appendf(
        "AP gate state: %s (%s)\nAP active: %s\nAP stable age: %lu ms\nParked: %s\nSummoning: %s\n",
        apGate.allowed ? "allowed" : "blocked", apGate.reason,
        apGate.apActive ? "yes" : "no", apGate.stableMs,
        apGate.parked ? "yes" : "no", apGate.summoning ? "yes" : "no");
    SummonInjectionDecision summonDecision = dashSummonOnlyInjectionDecision();
    report.appendf("Summon-only policy: %s\n", summonInjectionStateName(summonDecision.state));

    report.append("\n[Last Write Check]\n");
    report.appendf("State: %s\n", dashWriteProbeStateName(writeProbeSnapshot.state));
    if (writeProbeSnapshot.active)
    {
        report.appendf("CAN ID: 0x%03lX\nMux: %d\nWrite age: %lu ms\nWrite payload: ",
                       static_cast<unsigned long>(writeProbeSnapshot.id),
                       static_cast<int>(writeProbeSnapshot.mux), now - writeProbeSnapshot.txMs);
        dashAppendFrameBytes(report, writeProbeSnapshot.txData, writeProbeSnapshot.txDlc);
        report.append("\nBus payload: ");
        dashAppendFrameBytes(report, writeProbeSnapshot.rxData, writeProbeSnapshot.rxDlc);
        if (writeProbeSnapshot.hasRx)
            report.appendf("\nBus payload age: %lu ms", now - writeProbeSnapshot.rxMs);
        report.append("\n");
    }

    report.append("\n[Safety]\n");
    report.appendf("Hard torque cap: -1.80 to +1.80 Nm\nRaw torque bounds: 0x%03X to 0x%03X\n",
                   static_cast<unsigned int>(TORQUE_RAW_MIN),
                   static_cast<unsigned int>(TORQUE_RAW_MAX));
    report.append("Extended/RTR modification frames: rejected\n");

    report.append("\n[NVS]\n");
    report.appendf("State: %s%s\nInitial result: %ld (%s)\nFinal result: %ld (%s)\n",
                   nvsError ? "[ERROR] " : RuntimeDiagnostics::nvsState.load(std::memory_order_relaxed) == RuntimeDiagnostics::NvsState::Recovered ? "[WARN] "
                                                                                                                                                   : "[OK] ",
                   RuntimeDiagnostics::nvsStateName(),
                   static_cast<long>(RuntimeDiagnostics::nvsInitialError.load(std::memory_order_relaxed)),
                   esp_err_to_name(RuntimeDiagnostics::nvsInitialError.load(std::memory_order_relaxed)),
                   static_cast<long>(RuntimeDiagnostics::nvsFinalError.load(std::memory_order_relaxed)),
                   esp_err_to_name(RuntimeDiagnostics::nvsFinalError.load(std::memory_order_relaxed)));

    report.append("\n[USB Serial / SavvyCAN]\n");
    report.appendf("GVRET armed: %s\nGVRET connected: %s\nFrames sent: %lu\nFrames dropped: %lu\nBytes sent: %lu\nSession age: %lu ms\n",
                   GvretSerial::enabled.load(std::memory_order_relaxed) ? "yes" : "no",
                   GvretSerial::clientConnected.load(std::memory_order_relaxed) ? "yes" : "no",
                   static_cast<unsigned long>(GvretSerial::framesSent.load(std::memory_order_relaxed)),
                   static_cast<unsigned long>(GvretSerial::framesDropped.load(std::memory_order_relaxed)),
                   static_cast<unsigned long>(GvretSerial::bytesSent.load(std::memory_order_relaxed)),
                   static_cast<unsigned long>(GvretSerial::runtimeMs()));

    report.append("\n[Web]\n");
    report.appendf("HTTP requests: %lu\nHTTP response bytes: %lu (wraps at 4 GiB)\nLargest response: %lu bytes\nSupport requests: %lu\nPrevious support report: %lu bytes\nLast status JSON: %lu bytes\n",
                   static_cast<unsigned long>(server.requestCount()),
                   static_cast<unsigned long>(server.responseBytes()),
                   static_cast<unsigned long>(server.maxResponseBytes()),
                   static_cast<unsigned long>(RuntimeDiagnostics::supportRequests.load(std::memory_order_relaxed)),
                   static_cast<unsigned long>(RuntimeDiagnostics::lastSupportBytes.load(std::memory_order_relaxed)),
                   static_cast<unsigned long>(RuntimeDiagnostics::lastStatusBytes.load(std::memory_order_relaxed)));

    if (!report.complete())
    {
        server.send(500, "text/plain", "Support report exceeded fixed buffer; report not sent.");
        return;
    }
    RuntimeDiagnostics::lastSupportBytes.store(static_cast<uint32_t>(report.size()),
                                               std::memory_order_relaxed);
    dashSendBuffer(200, "text/plain; charset=utf-8", report.data(), report.size());
}
#endif

static void handleDevMode()
{
    if (server.hasArg("on") || server.hasArg("enabled"))
    {
        String v = server.hasArg("on") ? server.arg("on") : server.arg("enabled");
        dashSetDevMode(v == "1" || v == "true" || v == "on");
    }
    server.send(200, "application/json",
                String("{\"ok\":true,\"dev\":") + ((bool)dashDevMode ? "true" : "false") + "}");
}

static void handleBleMode()
{
    if (server.hasArg("on") || server.hasArg("enabled"))
    {
        String v = server.hasArg("on") ? server.arg("on") : server.arg("enabled");
        bool on = (v == "1" || v == "true" || v == "on");
        server.send(200, "application/json",
                    String("{\"ok\":true,\"ble\":") + (on ? "true" : "false") + ",\"reboot\":true}");
        dashSetBleMode(on); // reboots into the selected mode
        return;
    }
    server.send(200, "application/json",
                String("{\"ok\":true,\"ble\":") + ((bool)dashBleMode ? "true" : "false") + "}");
}

static void handleBlePin()
{
    if (server.hasArg("regen"))
    {
        uint32_t pin = 100000 + (esp_random() % 900000);
        dashBlePasskey = pin;
        prefs.begin(PREFS_NS, false);
        prefs.putString("ble_pin", String((unsigned long)pin));
        prefs.end();
        dashLog("[BLE] Pairing passkey regenerated");
    }
    server.send(200, "application/json",
                String("{\"ok\":true,\"pin\":") +
                    String((unsigned long)(uint32_t)dashBlePasskey) + "}");
}

// The configuration surface, shared by the HTTP dashboard and the BLE app.
//
// A phone reaches the device over BLE, where the dashboard does not run, so the
// same settings have to be readable and writable from both. The validation is
// safety-relevant -- Nag Mode C is blocked on HW4, and the speed profile range
// depends on the hardware -- so it exists exactly once and both transports run
// it, rather than being copied and left to drift.

/** Name/value lookup over whatever the transport carries. */
struct ConfigArgs
{
    virtual ~ConfigArgs() = default;
    virtual bool has(const char *name) const = 0;
    virtual String get(const char *name) const = 0;
};

/** HTTP status plus JSON body; the caller decides how to deliver it. */
struct ConfigResult
{
    int status;
    String body;
};

static String ctrlBuildConfigJson()
{
    CarManagerBase *handler = dashHandler;
    ExperimentalAssist::DryRunSnapshot dryRun;
    {
        DashDataGuard guard;
        dryRun = dashHandsOn247DryRun.snapshot();
    }
    String json = "{\"hw\":" + String(hwMode);
    json += ",\"dasLayout\":" + String(dashDasLayoutOverride);
    json += ",\"dasLayoutName\":\"" + String(dashDasLayoutName(dashDasLayoutOverride)) + "\"";
    json += ",\"speedProfile\":" + String(handler ? (int)handler->speedProfile : (int)dashManualSpeedProfile);
    json += ",\"speedAuto\":" + String(dashSpeedProfileAuto ? "true" : "false");
    json += ",\"injectionArmed\":" + String(canActive ? "true" : "false");
    json += ",\"pluginReplay\":" + String(pluginGetReplayCount());
    json += ",\"pluginReplayMax\":" + String(PLUGIN_REPLAY_COUNT_MAX);
    json += ",\"apGate\":" + String(apInjectionGate ? "true" : "false");
    json += ",\"summonOnly\":" + String(summonOnlyInjection ? "true" : "false");
    json += ",\"nagMode\":" + String(dashNagMode);
    json += ",\"ulcStalkConfirm\":" + String(dashUlcStalkConfirm ? "true" : "false");
    json += ",\"ulcOffHighway\":" + String(dashUlcOffHighway ? "true" : "false");
    json += ",\"ulcSpeedConfig\":" + String(dashUlcSpeedConfig <= 3 ? static_cast<int>(dashUlcSpeedConfig) : -1);
    json += ",\"ulcBlindSpotConfig\":" + String(dashUlcBlindSpotConfig <= 2 ? static_cast<int>(dashUlcBlindSpotConfig) : -1);
    json += ",\"summonEuUnlock\":" + String(dashSummonEuUnlock ? "true" : "false");
    json += ",\"handsOn247DryRun\":" + String(dryRun.enabled ? "true" : "false");
    json += ",\"handsOn247Stats\":{\"frames247\":" + String(dryRun.frames247);
    json += ",\"frames3e9\":" + String(dryRun.frames3e9);
    json += ",\"nearbyEvents\":" + String(dryRun.nearbyEvents) + "}";
    json += ",\"hw3OffsetSlew\":" + String(hw3OffsetSlew ? "true" : "false");
    json += ",\"hw3SlewRate\":" + String(hw3SlewRate);
    json += ",\"ledBrightness\":" + String(dashLedBrightness);
    json += "}";
    return json;
}

static void handleConfigGet()
{
    server.send(200, "application/json", ctrlBuildConfigJson());
}

static ConfigResult ctrlApplyConfig(const ConfigArgs &args)
{
    long hwValue = hwMode;
    long dasLayoutValue = dashDasLayoutOverride;
    long speedValue = dashManualSpeedProfile;
    long replayValue = pluginGetReplayCount();
    long nagModeValue = dashNagMode;
    long slewRateValue = hw3SlewRate;
    long ulcSpeedValue = dashUlcSpeedConfig <= 3 ? static_cast<long>(dashUlcSpeedConfig) : -1;
    long ulcBlindValue = dashUlcBlindSpotConfig <= 2 ? static_cast<long>(dashUlcBlindSpotConfig) : -1;
    bool canValue = canActive;
    bool speedAutoValue = dashSpeedProfileAuto;
    bool gateValue = apInjectionGate;
    bool summonOnlyValue = summonOnlyInjection;
    bool slewValue = hw3OffsetSlew;
    bool ulcStalkValue = dashUlcStalkConfirm;
    bool ulcOffHighwayValue = dashUlcOffHighway;
    bool summonEuValue = dashSummonEuUnlock;
    bool hands247Value = dashHandsOn247DryRunEnabled;
    const char *slewArg = args.has("hw3OffsetSlew") ? "hw3OffsetSlew" : "offsetSlew";
    const char *slewRateArg = args.has("hw3SlewRate") ? "hw3SlewRate" : "offsetSlewRate";
    bool valid = true;
    if (args.has("hw"))
        valid &= dashParseLong(args.get("hw"), hwValue) && hwValue >= 0 && hwValue <= 2;
    if (args.has("dasLayout"))
        valid &= dashParseLong(args.get("dasLayout"), dasLayoutValue) &&
                 dasLayoutValue >= 0 && dasLayoutValue <= 3;
    if (args.has("sp"))
        valid &= dashParseLong(args.get("sp"), speedValue) && speedValue >= 0 &&
                 speedValue <= (hwValue == 2 ? 4 : 2);
    if (args.has("plgr"))
        valid &= dashParseLong(args.get("plgr"), replayValue) && replayValue >= 1 &&
                 replayValue <= PLUGIN_REPLAY_COUNT_MAX;
    if (args.has("nag"))
        valid &= dashParseLong(args.get("nag"), nagModeValue) &&
                 nagModeValue >= static_cast<long>(NagMode::Disabled) &&
                 nagModeValue <= static_cast<long>(NagMode::ModeC);
    if (args.has("ulcSpeed"))
        valid &= dashParseLong(args.get("ulcSpeed"), ulcSpeedValue) &&
                 ulcSpeedValue >= -1 && ulcSpeedValue <= 3;
    if (args.has("ulcBlind"))
        valid &= dashParseLong(args.get("ulcBlind"), ulcBlindValue) &&
                 ulcBlindValue >= -1 && ulcBlindValue <= 2;
    if (args.has("hw3SlewRate") || args.has("offsetSlewRate"))
        valid &= dashParseLong(args.get(slewRateArg), slewRateValue) &&
                 slewRateValue >= kHw3SlewRateMin && slewRateValue <= kHw3SlewRateMax;
    if (args.has("can"))
        valid &= dashParseBool(args.get("can"), canValue);
    if (args.has("spa"))
        valid &= dashParseBool(args.get("spa"), speedAutoValue);
    if (args.has("apg"))
        valid &= dashParseBool(args.get("apg"), gateValue);
    if (args.has("smo"))
        valid &= dashParseBool(args.get("smo"), summonOnlyValue);
    if (args.has("ulcStalk"))
        valid &= dashParseBool(args.get("ulcStalk"), ulcStalkValue);
    if (args.has("ulcOffHighway"))
        valid &= dashParseBool(args.get("ulcOffHighway"), ulcOffHighwayValue);
    if (args.has("summonEu"))
        valid &= dashParseBool(args.get("summonEu"), summonEuValue);
    if (args.has("hands247"))
        valid &= dashParseBool(args.get("hands247"), hands247Value);
    if (args.has("hw3OffsetSlew") || args.has("offsetSlew"))
        valid &= dashParseBool(args.get(slewArg), slewValue);
    if (!valid)
    {
        return {400, String("{\"ok\":false,\"error\":\"Invalid configuration value\"}")};
    }
    if (args.has("nag") &&
        !nagModeAllowedForHardware(static_cast<uint8_t>(nagModeValue),
                                   static_cast<uint8_t>(hwValue)))
    {
        return {400,
                String("{\"ok\":false,\"error\":\"Nag Mode C is blocked on HW4 after reported control faults\"}")};
    }
    if (args.has("summonEu") && summonEuValue && hwValue != 2)
    {
        return {400,
                String("{\"ok\":false,\"error\":\"Summon EU flag is experimental and HW4-only\"}")};
    }

    AppHandlerGuard appGuard;
    uint8_t oldHw = hwMode;
    uint8_t oldDasLayout = dashDasLayoutOverride;
    bool oldCan = canActive;
    bool oldSpeedAuto = dashSpeedProfileAuto;
    uint8_t oldSpeed = dashManualSpeedProfile;
    bool oldGate = apInjectionGate;
    bool oldSummonOnly = summonOnlyInjection;
    uint8_t oldNagMode = dashNagMode;
    bool oldUlcStalk = dashUlcStalkConfirm;
    bool oldUlcOffHighway = dashUlcOffHighway;
    uint8_t oldUlcSpeed = dashUlcSpeedConfig;
    uint8_t oldUlcBlind = dashUlcBlindSpotConfig;
    bool oldSummonEu = dashSummonEuUnlock;
    bool oldHands247 = dashHandsOn247DryRunEnabled;
    uint8_t oldReplay = pluginGetReplayCount();
    bool oldSlew = hw3OffsetSlew;
    uint8_t oldSlewRate = hw3SlewRate;
    bool hwChanged = false;
    bool speedManualLogged = false;
    bool speedAutoLogged = false;
    bool gateLogged = false;
    bool summonOnlyLogged = false;
    bool nagLogged = false;
    bool replayLogged = false;
    bool slewLogged = false;
    bool slewRateLogged = false;
    uint8_t loggedSpeed = 0;
    bool loggedSpeedAuto = false;
    bool loggedGate = false;
    bool loggedSummonOnly = false;
    uint8_t loggedNag = 0;
    uint8_t loggedReplay = 0;
    bool loggedSlew = false;
    uint8_t loggedSlewRate = 0;
    bool nagChanged = false;
    bool experimentalChanged = false;
    {
        PluginLockGuard pluginGuard;
        DashDataGuard dataGuard;
        DashRecorderConfigUpdate recorderUpdate(dashRecorder);
        const uint32_t commitNow = millis();
        if (args.has("hw"))
        {
            uint8_t v = static_cast<uint8_t>(hwValue);
            if (v <= 2 && v != hwMode)
            {
                hwMode = v;
                hwChanged = true;
            }
        }
        if (args.has("dasLayout"))
            dashDasLayoutOverride = dashClampDasLayoutOverride(static_cast<int>(dasLayoutValue));
        if (args.has("can"))
            canActive = canValue;
        bool profileAutoRequested = args.has("spa") && speedAutoValue;
        if (args.has("sp"))
        {
            uint8_t v = static_cast<uint8_t>(speedValue);
            if (!profileAutoRequested && (v != dashManualSpeedProfile || dashSpeedProfileAuto))
            {
                speedManualLogged = true;
                loggedSpeed = v;
            }
            dashManualSpeedProfile = v;
            if (!profileAutoRequested)
                dashSpeedProfileAuto = false;
        }
        if (args.has("spa"))
        {
            bool v = speedAutoValue;
            if (v != dashSpeedProfileAuto)
            {
                speedAutoLogged = true;
                loggedSpeedAuto = v;
            }
            dashSpeedProfileAuto = v;
        }
        if (args.has("apg"))
        {
            bool v = gateValue;
            if (v != apInjectionGate)
            {
                apInjectionGate = v;
                gateLogged = true;
                loggedGate = v;
            }
        }
        if (args.has("smo"))
        {
            bool v = summonOnlyValue;
            if (v != summonOnlyInjection)
            {
                summonOnlyInjection = v;
                summonOnlyLogged = true;
                loggedSummonOnly = v;
            }
        }
        if (args.has("nag"))
        {
            uint8_t v = static_cast<uint8_t>(nagModeValue);
            if (v != dashNagMode)
            {
                dashNagMode = v;
                nagLogged = true;
                loggedNag = v;
                nagChanged = true;
            }
        }
        if (args.has("ulcStalk"))
            dashUlcStalkConfirm = ulcStalkValue;
        if (args.has("ulcOffHighway"))
            dashUlcOffHighway = ulcOffHighwayValue;
        if (args.has("ulcSpeed"))
            dashUlcSpeedConfig = ulcSpeedValue < 0
                                     ? ExperimentalAssist::kPreserveSetting
                                     : static_cast<uint8_t>(ulcSpeedValue);
        if (args.has("ulcBlind"))
            dashUlcBlindSpotConfig = ulcBlindValue < 0
                                         ? ExperimentalAssist::kPreserveSetting
                                         : static_cast<uint8_t>(ulcBlindValue);
        if (args.has("summonEu"))
            dashSummonEuUnlock = summonEuValue;
        if (args.has("hands247"))
            dashHandsOn247DryRunEnabled = hands247Value;
        if (hwMode != 2)
            dashSummonEuUnlock = false;
        experimentalChanged = oldUlcStalk != static_cast<bool>(dashUlcStalkConfirm) ||
                              oldUlcOffHighway != static_cast<bool>(dashUlcOffHighway) ||
                              oldUlcSpeed != static_cast<uint8_t>(dashUlcSpeedConfig) ||
                              oldUlcBlind != static_cast<uint8_t>(dashUlcBlindSpotConfig) ||
                              oldSummonEu != static_cast<bool>(dashSummonEuUnlock) ||
                              oldHands247 != static_cast<bool>(dashHandsOn247DryRunEnabled);
        if (args.has("plgr"))
        {
            uint8_t previous = pluginGetReplayCountLocked();
            pluginSetReplayCountLocked(replayValue);
            if (pluginGetReplayCountLocked() != previous)
            {
                replayLogged = true;
                loggedReplay = pluginGetReplayCountLocked();
            }
        }
        if (args.has("hw3OffsetSlew") || args.has("offsetSlew"))
        {
            bool v = slewValue;
            if (v != hw3OffsetSlew)
            {
                hw3OffsetSlew = v;
                slewLogged = true;
                loggedSlew = v;
            }
        }
        if (args.has("hw3SlewRate") || args.has("offsetSlewRate"))
        {
            uint8_t v = static_cast<uint8_t>(slewRateValue);
            if (v != hw3SlewRate)
            {
                hw3SlewRate = v;
                slewRateLogged = true;
                loggedSlewRate = v;
            }
        }
        // The effective mutation and its recorder publication share one
        // timestamp while AppHandler -> Plugin -> Dash locks are held.
        dashApplyRuntimeState(false);
        dashRecordEffectiveConfigurationAt(commitNow, pluginGetReplayCountLocked());
    }
#if defined(DASH_RGB_STATUS_LED)
    appRefreshStatusLed();
#endif

    if (hwChanged)
        dashLog("[CFG] HW=" + String(hwMode == 0 ? "LEGACY" : hwMode == 1 ? "HW3" : "HW4"));
    if (speedManualLogged)
        dashLog("[CFG] Speed profile manual " + String(loggedSpeed));
    if (speedAutoLogged)
        dashLog("[CFG] Speed profile " + String(loggedSpeedAuto ? "AUTO" : "MANUAL"));
    if (gateLogged)
        dashLog("[CFG] AP injection gate " + String(loggedGate ? "ON" : "OFF"));
    if (summonOnlyLogged)
        dashLog("[CFG] Summon-only injection " + String(loggedSummonOnly ? "ON" : "OFF"));
    if (nagLogged)
        dashLog("[CFG] Nag suppression " + String(nagModeName(loggedNag)));
    if (replayLogged)
        dashLog("[CFG] Plugin replay x" + String(loggedReplay));
    if (slewLogged)
        dashLog("[CFG] Offset slew " + String(loggedSlew ? "ON" : "OFF"));
    if (slewRateLogged)
        dashLog("[CFG] Offset slew rate " + String(loggedSlewRate) + "%/s");
    if (experimentalChanged)
        dashLog("[CFG] Experimental assist controls updated; TX session rotated");

    if (nagChanged)
        dashReapplyFiltersWithPlugins();
    if (hwChanged)
    {
        dashSwapHandler(hwMode);
        dashApplyFilters();
    }
    {
        DashDataGuard dataGuard;
        dashApplyRuntimeState();
        dashRecordEffectiveConfiguration();
        dashRefreshSummonOnlyPolicy();
    }
    if (!dashSavePrefs())
    {
        {
            PluginLockGuard pluginGuard;
            DashDataGuard dataGuard;
            DashRecorderConfigUpdate recorderUpdate(dashRecorder);
            hwMode = oldHw;
            dashDasLayoutOverride = oldDasLayout;
            canActive = oldCan;
            dashSpeedProfileAuto = oldSpeedAuto;
            dashManualSpeedProfile = oldSpeed;
            apInjectionGate = oldGate;
            summonOnlyInjection = oldSummonOnly;
            dashNagMode = oldNagMode;
            dashUlcStalkConfirm = oldUlcStalk;
            dashUlcOffHighway = oldUlcOffHighway;
            dashUlcSpeedConfig = oldUlcSpeed;
            dashUlcBlindSpotConfig = oldUlcBlind;
            dashSummonEuUnlock = oldSummonEu;
            dashHandsOn247DryRunEnabled = oldHands247;
            pluginSetReplayCountLocked(oldReplay);
            hw3OffsetSlew = oldSlew;
            hw3SlewRate = oldSlewRate;
        }
        if (hwChanged)
        {
            dashSwapHandler(oldHw);
            dashApplyFilters();
        }
        {
            DashDataGuard dataGuard;
            dashApplyRuntimeState();
            dashRecordEffectiveConfiguration();
            dashRefreshSummonOnlyPolicy();
        }
        dashReapplyFiltersWithPlugins();
        return {500, String("{\"ok\":false,\"error\":\"NVS write failed\"}")};
    }
    return {200, String("{\"ok\":true}")};
}

static void handleConfig()
{
    // Reads straight from the query string of the request being handled.
    struct ServerArgs : ConfigArgs
    {
        bool has(const char *name) const override { return server.hasArg(name); }
        String get(const char *name) const override { return server.arg(name); }
    } args;
    ConfigResult result = ctrlApplyConfig(args);
    server.send(result.status, "application/json", result.body);
}

static void handleLedBrightness()
{
    if (!server.hasArg("b"))
    {
        server.send(400, "application/json", "{\"ok\":false,\"err\":\"missing b\"}");
        return;
    }
    long raw = 0;
    if (!dashParseLong(server.arg("b"), raw) || raw < 0 || raw > 255)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Brightness must be 0-255\"}");
        return;
    }
    uint8_t v = static_cast<uint8_t>(raw);
    if (v != dashLedBrightness)
    {
        uint8_t previous = dashLedBrightness;
        dashLedBrightness = v;
        dashLog("[CFG] LED brightness " + String(v));
        if (!dashSavePrefs())
        {
            dashLedBrightness = previous;
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
            return;
        }
#if defined(DASH_RGB_STATUS_LED)
        appRefreshStatusLed(true);
#endif
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleDisable()
{
    bool saved = dashSetCanActive(false, "dashboard");
    server.send(saved ? 200 : 500, "text/plain", saved ? "Injection stopped." : "Injection stopped; NVS write failed.");
}

static void handleReboot()
{
    server.send(200, "text/plain", "Rebooting...");
    delay(200);
    ESP.restart();
}

#ifdef ESP_PLATFORM
static void handleGvretStatus()
{
    String json = "{\"enabled\":";
    json += GvretSerial::enabled.load(std::memory_order_relaxed) ? "true" : "false";
    json += ",\"connected\":";
    json += GvretSerial::clientConnected.load(std::memory_order_relaxed) ? "true" : "false";
    json += ",\"frames\":" + String(GvretSerial::framesSent.load(std::memory_order_relaxed));
    json += ",\"dropped\":" + String(GvretSerial::framesDropped.load(std::memory_order_relaxed));
    json += ",\"bytes\":" + String(GvretSerial::bytesSent.load(std::memory_order_relaxed));
    json += ",\"runtimeMs\":" + String(GvretSerial::runtimeMs());
    json += ",\"maxRuntimeMs\":" + String(GvretSerial::kMaxRunMs);
    json += ",\"protocol\":\"GVRET serial\",\"baud\":115200}";
    server.send(200, "application/json", json);
}

static void handleGvretStart()
{
    bool ok = GvretSerial::start();
    server.send(ok ? 200 : 500, "application/json",
                ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"GVRET task unavailable\"}");
}

static void handleGvretStop()
{
    GvretSerial::stop();
    server.send(200, "application/json", "{\"ok\":true}");
}
#endif

static void dashResumeTransmitAfterOtaFailure();

static void handleOtaResult()
{
    if (!server.authenticate(DASH_OTA_USER, DASH_OTA_PASS))
    {
        server.requestAuthentication();
        return;
    }
    bool ok = Update.isFinished() && !Update.hasError();
    server.sendHeader("Connection", "close");
    server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAIL");
    if (ok)
    {
        dashLog("[OTA] Upload complete -- rebooting");
        delay(300);
        ESP.restart();
    }
    else
    {
        dashResumeTransmitAfterOtaFailure();
        dashLog("[OTA] Upload FAILED");
    }
}

static bool manualOtaAccepted = false;

// OTA is a temporary maintenance gate, not a configuration change. Preserve
// the user's persisted master enable choice, but discard every queued or
// periodic transmission before Update can start. After reboot, the normal
// startup/CAN freshness gates decide when that desired state becomes effective.
static void dashQuiesceTransmitForOta(const char *source)
{
    appMaintenanceTxInhibit = true;
    pluginResetPeriodicEmit();
    if (dashDriver)
        dashDriver->clearPendingTransmit();
    if (appDashboardDecisionObserver)
        appDashboardDecisionObserver(false, "device_ota");
    String message = "[OTA] TX paused and pending work cleared";
    if (source && *source)
        message += String(" via ") + source;
    dashLog(message);
}

static void dashResumeTransmitAfterOtaFailure()
{
    const RuntimeDiagnostics::OtaBootState otaState =
        RuntimeDiagnostics::otaBootState.load(std::memory_order_relaxed);
    if (otaState == RuntimeDiagnostics::OtaBootState::Pending ||
        otaState == RuntimeDiagnostics::OtaBootState::Rollback)
        return;
    appMaintenanceTxInhibit = false;
}

class DashMaintenanceCleanupGuard
{
public:
    explicit DashMaintenanceCleanupGuard(bool invalidateFreshness = false)
        : invalidateFreshness_(invalidateFreshness) {}

    ~DashMaintenanceCleanupGuard()
    {
        if (invalidateFreshness_)
        {
            RuntimeDiagnostics::invalidateCanFreshness();
            if (dashDriver && dashDriver->ready())
                RuntimeDiagnostics::noteCanInitialized();
        }
        if (!keepInhibited_)
            dashResumeTransmitAfterOtaFailure();
    }

    void keepInhibited() { keepInhibited_ = true; }

private:
    bool invalidateFreshness_ = false;
    bool keepInhibited_ = false;
};

static void handleOtaUpload()
{
    if (!server.authenticate(DASH_OTA_USER, DASH_OTA_PASS))
        return;
    DashMaintenanceCleanupGuard cleanup;
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
        if (upload.filename != FIRMWARE_ARTIFACT)
        {
            manualOtaAccepted = false;
            dashLog("[OTA] Rejected artifact: " + String(upload.filename.c_str()) +
                    " expected " + String(FIRMWARE_ARTIFACT));
            return;
        }
        dashLog("[OTA] Receiving: " + String(upload.filename.c_str()));
        dashQuiesceTransmitForOta("web_upload");
        manualOtaAccepted = Update.begin(upload.totalSize);
        if (!manualOtaAccepted)
        {
            dashResumeTransmitAfterOtaFailure();
            dashLog("[OTA] Begin failed");
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (manualOtaAccepted && Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        {
            manualOtaAccepted = false;
            Update.abort();
            dashResumeTransmitAfterOtaFailure();
            dashLog("[OTA] Write error");
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        bool accepted = manualOtaAccepted;
        manualOtaAccepted = false;
        if (accepted && Update.end(true))
            dashLog("[OTA] Done: " + String(upload.totalSize) + " bytes");
        else
        {
            dashResumeTransmitAfterOtaFailure();
            dashLog("[OTA] End failed");
        }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
        bool accepted = manualOtaAccepted;
        manualOtaAccepted = false;
        if (accepted)
            Update.abort();
        dashResumeTransmitAfterOtaFailure();
        dashLog("[OTA] Upload aborted");
    }
    if (manualOtaAccepted || (Update.isFinished() && !Update.hasError()))
        cleanup.keepInhibited();
}

static void handleCanSelfTest()
{
    if (!server.authenticate(DASH_OTA_USER, DASH_OTA_PASS))
    {
        server.requestAuthentication();
        return;
    }
    if (Update.isRunning())
    {
        server.send(409, "application/json",
                    "{\"ok\":false,\"error\":\"OTA in progress\"}");
        return;
    }

    appMaintenanceTxInhibit = true;
    DashMaintenanceCleanupGuard cleanup(true);
    pluginResetPeriodicEmit();
    if (dashDriver)
        dashDriver->clearPendingTransmit();
    char result[640] = {};
    if (dashDriver)
        dashDriver->selfTestJson(result, sizeof(result));
    else
        snprintf(result, sizeof(result),
                 "{\"supported\":false,\"passed\":false,\"reason\":\"driver_unavailable\"}");
    dashLog("[SELFTEST] CAN controller test completed without physical TX");
    server.send(200, "application/json", String("{\"ok\":true,\"result\":") + result + "}");
}

// ── PLUGIN MANAGEMENT ───────────────────────────────────────────

static void dashReapplyFiltersWithPlugins()
{
    if (!dashHandler || !dashDriver)
        return;
    uint32_t mergedIds[160];
    const uint8_t count =
        dashCollectMergedFilterIds(mergedIds, sizeof(mergedIds) / sizeof(mergedIds[0]));
    dashDriver->setFilters(mergedIds, count);
}

static uint8_t dashCollectMergedFilterIds(uint32_t *ids, uint8_t maxIds)
{
    if (!dashHandler || !ids || maxIds == 0)
        return 0;
    uint8_t count = 0;
    const uint32_t *hIds = dashHandler->filterIds();
    uint8_t hCount = dashHandler->filterIdCount();
    for (uint8_t i = 0; i < hCount && count < maxIds; i++)
        ids[count++] = hIds[i];
    if (dashNagMode != static_cast<uint8_t>(NagMode::Disabled))
    {
        const uint32_t *nagIds = dashNagHandler.modeFilterIds();
        uint8_t nagCount = dashNagHandler.modeFilterIdCount(dashNagMode);
        for (uint8_t i = 0; i < nagCount && count < maxIds; i++)
            ids[count++] = nagIds[i];
    }
    count += pluginGetFilterIds(ids + count, maxIds - count);
    uint8_t uniqueCount = 0;
    for (uint8_t i = 0; i < count; i++)
    {
        bool duplicate = false;
        for (uint8_t j = 0; j < uniqueCount; j++)
        {
            if (ids[j] == ids[i])
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            ids[uniqueCount++] = ids[i];
    }
    return uniqueCount;
}

static void handlePluginList()
{
    struct PluginSummary
    {
        char name[PLUGIN_NAME_MAX];
        char version[16];
        char author[32];
        uint8_t ruleCount;
        bool enabled;
    };
    PluginSummary snapshot[PLUGIN_MAX] = {};
    uint8_t count = 0;
    {
        PluginLockGuard guard;
        count = pluginCount;
        for (uint8_t i = 0; i < count; i++)
        {
            strlcpy(snapshot[i].name, pluginStore[i].name, sizeof(snapshot[i].name));
            strlcpy(snapshot[i].version, pluginStore[i].version, sizeof(snapshot[i].version));
            strlcpy(snapshot[i].author, pluginStore[i].author, sizeof(snapshot[i].author));
            snapshot[i].ruleCount = pluginStore[i].ruleCount;
            snapshot[i].enabled = pluginStore[i].enabled;
        }
    }
    String j = "{\"maxPlugins\":" + String(PLUGIN_MAX) + ",\"plugins\":[";
    for (uint8_t i = 0; i < count; i++)
    {
        if (i)
            j += ",";
        j += "{\"name\":\"" + jsonEscape(snapshot[i].name) + "\"";
        j += ",\"version\":\"" + jsonEscape(snapshot[i].version) + "\"";
        j += ",\"author\":\"" + jsonEscape(snapshot[i].author) + "\"";
        j += ",\"rules\":" + String(snapshot[i].ruleCount);
        j += ",\"priority\":" + String(i + 1);
        j += ",\"enabled\":" + String(snapshot[i].enabled ? "true" : "false") + "}";
    }
    j += "]}";
    server.send(200, "application/json", j);
}

static bool pluginInstallJson(const String &json, const String &url)
{
    PluginData temp;
    if (!pluginParseJson(json, temp))
        return false;

    // Check for duplicate name
    int existing = pluginFindByName(temp.name);
    if (existing < 0 && pluginCount >= PLUGIN_MAX)
        return false;

    uint8_t insertIndex = pluginCount;
    String oldFilename;
    if (existing >= 0)
    {
        insertIndex = (uint8_t)existing;
        oldFilename = pluginStore[existing].filename;
    }

    // Keep path under SPIFFS 31-character object name limit: "/p_" + base + ".json".
    String fname;
    for (size_t i = 0; temp.name[i] != '\0'; i++)
    {
        char c = temp.name[i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        fname += allowed ? c : '_';
    }
    const size_t maxSpiffsPathLen = 31;
    const size_t prefixLen = 3; // "/p_"
    const size_t suffixLen = 5; // ".json"
    const size_t maxBaseLen = maxSpiffsPathLen - prefixLen - suffixLen;
    if (fname.length() > maxBaseLen)
        fname = fname.substring(0, maxBaseLen);
    fname += ".json";
    strlcpy(temp.filename, fname.c_str(), sizeof(temp.filename));
    strlcpy(temp.sourceUrl, url.c_str(), sizeof(temp.sourceUrl));
    temp.enabled = false;
    temp.priority = insertIndex;

    {
        PluginLockGuard guard;
        for (uint8_t i = 0; i < pluginCount; i++)
        {
            if (i != insertIndex && strcmp(pluginStore[i].filename, temp.filename) == 0)
                return false;
        }
    }

    if (!pluginSaveToSpiffs(json, temp.filename))
        return false;

    if (existing >= 0)
    {
        if (oldFilename.length() > 0 && oldFilename != temp.filename)
        {
            if (!SPIFFS.remove(pluginFilePath(oldFilename.c_str())))
            {
                SPIFFS.remove(pluginFilePath(temp.filename));
                return false;
            }
        }
        PluginLockGuard guard;
        pluginsLocked = true;
        pluginStore[insertIndex] = temp;
        pluginNormalizePriorities();
        pluginResetDiagnostics();
        pluginsLocked = false;
    }
    else if (!pluginInsert(pluginCount, temp))
    {
        SPIFFS.remove(pluginFilePath(temp.filename));
        return false;
    }

    dashSaveAllPluginStates();
    pluginResetDiagnostics();
    pluginResetPeriodicEmit();

    dashReapplyFiltersWithPlugins();
    dashLog("[PLG] Installed: " + String(temp.name) + " (" + String(temp.ruleCount) + " rules)");
    return true;
}

static void handlePluginUpload()
{
    String json = server.arg("plain");
    if (json.length() == 0)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
        return;
    }
    if (pluginInstallJson(json, ""))
        server.send(200, "application/json", "{\"ok\":true}");
    else
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid plugin JSON or max " + String(PLUGIN_MAX) + " plugins reached\"}");
}

static void handlePluginInstallFromUrl()
{
    String url = server.arg("url");
    if (url.length() == 0)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"no url\"}");
        return;
    }
    if (url.length() > 1024 || !url.startsWith("https://"))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"HTTPS URL required\"}");
        return;
    }
    if (!dashStaConnectedSnapshot())
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"WiFi not connected. Configure WiFi first.\"}");
        return;
    }

    HTTPClient http;
    WiFiClientSecure client;
#ifndef ESP_PLATFORM
    client.setInsecure(); // skip cert verification for simplicity
#endif
    http.setTimeout(15000);
    http.begin(client, url);
    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        String err = "HTTP " + String(code);
        http.end();
        dashLog("[PLG] Download failed: " + err);
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"" + err + "\"}");
        return;
    }
    String json = http.getString();
    http.end();

    if (pluginInstallJson(json, url))
        server.send(200, "application/json", "{\"ok\":true}");
    else
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid plugin JSON or max " + String(PLUGIN_MAX) + " plugins reached\"}");
}

static void handlePluginToggle()
{
    if (!server.hasArg("idx"))
    {
        server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    long parsedIdx = -1;
    if (!dashParseLong(server.arg("idx"), parsedIdx) || parsedIdx < 0 || parsedIdx > 255)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid idx\"}");
        return;
    }
    uint8_t idx = static_cast<uint8_t>(parsedIdx);
    if (idx < pluginCount)
    {
        bool enabled = false;
        char name[PLUGIN_NAME_MAX] = {};
        {
            PluginLockGuard guard;
            pluginStore[idx].enabled = !pluginStore[idx].enabled;
            enabled = pluginStore[idx].enabled;
            strlcpy(name, pluginStore[idx].name, sizeof(name));
        }
        pluginResetDiagnostics();
        pluginResetPeriodicEmit();
        dashSchedulePluginStateSave();
        dashReapplyFiltersWithPlugins();
        dashLog("[PLG] " + String(name) + " " + String(enabled ? "enabled" : "disabled"));
        server.send(200, "application/json",
                    String("{\"ok\":true,\"enabled\":") +
                        (enabled ? "true" : "false") + "}");
        return;
    }
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid idx\"}");
}

static void handlePluginRemove()
{
    if (!server.hasArg("idx"))
    {
        server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    long parsedIdx = -1;
    if (!dashParseLong(server.arg("idx"), parsedIdx) || parsedIdx < 0 || parsedIdx > 255)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid idx\"}");
        return;
    }
    uint8_t idx = static_cast<uint8_t>(parsedIdx);
    if (idx < pluginCount)
    {
        PluginData removedPlugin;
        {
            PluginLockGuard guard;
            removedPlugin = pluginStore[idx];
        }
        String name = removedPlugin.name;
        if (!dashClearPluginState(removedPlugin))
        {
            dashSaveAllPluginStates();
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"Plugin removal failed\"}");
            return;
        }
        if (!pluginRemove(idx))
        {
            dashSaveAllPluginStates();
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"Plugin removal failed\"}");
            return;
        }
        pluginResetPeriodicEmit();
        if (!dashSaveAllPluginStates())
            dashLog("[WARN] Plugin order persistence failed");
        dashReapplyFiltersWithPlugins();
        dashLog("[PLG] Removed: " + name);
        server.send(200, "application/json", "{\"ok\":true}");
        return;
    }
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid idx\"}");
}

static void handlePluginPriority()
{
    if (!server.hasArg("idx") || !server.hasArg("priority"))
    {
        server.send(400, "application/json", "{\"ok\":false}");
        return;
    }

    long idx = -1, priority = -1;
    if (!dashParseLong(server.arg("idx"), idx) || !dashParseLong(server.arg("priority"), priority))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid priority\"}");
        return;
    }
    if (idx < 0 || idx >= pluginCount || priority < 0 || priority >= pluginCount)
    {
        server.send(400, "application/json", "{\"ok\":false}");
        return;
    }

    if (pluginMove((uint8_t)idx, (uint8_t)priority))
    {
        if (!dashSaveAllPluginStates())
        {
            pluginMove((uint8_t)priority, (uint8_t)idx);
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"Plugin priority persistence failed\"}");
            return;
        }
        pluginResetPeriodicEmit();
        dashReapplyFiltersWithPlugins();
        char movedName[32] = {};
        {
            PluginLockGuard guard;
            strlcpy(movedName, pluginStore[priority].name, sizeof(movedName));
        }
        dashLog("[PLG] Priority: " + String(movedName) + " #" + String(priority + 1));
        server.send(200, "application/json", "{\"ok\":true}");
        return;
    }

    server.send(400, "application/json", "{\"ok\":false}");
}

// ── WIFI STA ────────────────────────────────────────────────────

static bool dashStartAccessPoint(bool withSta)
{
    DashWifiGuard guard;
    WiFi.persistent(false);
    WiFi.mode(withSta ? WIFI_AP_STA : WIFI_AP);
    WiFi.setSleep(false);

    IPAddress apIp(192, 168, 4, 1);
    IPAddress apMask(255, 255, 255, 0);
    WiFi.softAPConfig(apIp, apIp, apMask);

    if (!dashApConfigValid(apSSID, apPass))
        dashUseDefaultApConfig();

    bool ok = WiFi.softAP(apSSID, apPass, kDashApChannel, apHidden ? 1 : 0, kDashApMaxConn);
    if (!ok)
    {
        dashUseDefaultApConfig();
        ok = WiFi.softAP(apSSID, apPass, kDashApChannel, 0, kDashApMaxConn);
    }
    if (!ok)
        dashLog("[WIFI] AP start failed");
    return ok;
}

static void dashBeginSTA()
{
    DashWifiGuard guard;
    if (strlen(staSSID) == 0)
        return;

    if (staStaticIP && (uint32_t)staIP != 0)
    {
        if (WiFi.config(staIP, staGW, staMask, staDNS))
            dashLog("[WIFI] Static IP: " + staIP.toString());
        else
        {
            staStaticIP = false;
            if (!WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE))
            {
                dashLog("[WIFI] Network interface configuration failed");
                staRetryAt = millis() + kDashStaRetryMs;
                return;
            }
            dashLog("[WIFI] Static IP failed; using DHCP");
        }
    }
    else
    {
        if (!WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE))
        {
            dashLog("[WIFI] DHCP configuration failed");
            staRetryAt = millis() + kDashStaRetryMs;
            return;
        }
    }
    WiFi.begin(staSSID, staPass);
    staConnectAttemptActive = true;
    staConnectStartedAt = millis();
    staRetryAt = 0;
    dashLog("[WIFI] Connecting to " + String(staSSID) + "...");
}

static void dashPrepareStaReconnect()
{
    DashWifiGuard guard;
    if (staConnectAttemptActive || staConnected || WiFi.status() == WL_CONNECTED)
        WiFi.disconnect(false, false);
    staConnected = false;
    staConnectAttemptActive = false;
    staRetryAt = 0;
    autoUpdateEligibleAt = 0;
}

#ifdef ESP_PLATFORM
static void dashStopDiscovery()
{
    if (dashDiscoverySocket >= 0)
    {
        close(dashDiscoverySocket);
        dashDiscoverySocket = -1;
    }
}

static void dashStartDiscovery()
{
    if (dashBleMode || dashDiscoverySocket >= 0)
        return;
    const int socketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketFd < 0)
    {
        dashLog("[DISCOVERY] UDP socket creation failed");
        return;
    }
    int reuse = 1;
    if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
    {
        close(socketFd);
        dashLog("[DISCOVERY] UDP socket option failed");
        return;
    }
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(kDashDiscoveryPort);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socketFd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
    {
        close(socketFd);
        dashLog("[DISCOVERY] UDP bind failed");
        return;
    }
    const int flags = fcntl(socketFd, F_GETFL, 0);
    if (flags < 0 || fcntl(socketFd, F_SETFL, flags | O_NONBLOCK) != 0)
    {
        close(socketFd);
        dashLog("[DISCOVERY] UDP non-blocking setup failed");
        return;
    }
    dashDiscoverySocket = socketFd;
    dashLog("[DISCOVERY] UDP listener ready");
}

static void dashDiscoveryTick()
{
    if (dashDiscoverySocket < 0)
        return;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        char request[sizeof(kDashDiscoveryRequest)] = {};
        sockaddr_in peer = {};
        socklen_t peerLength = sizeof(peer);
        const int received = recvfrom(dashDiscoverySocket, request, sizeof(request), 0,
                                      reinterpret_cast<sockaddr *>(&peer), &peerLength);
        if (received < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            dashStopDiscovery();
            return;
        }
        if (received != static_cast<int>(strlen(kDashDiscoveryRequest)) ||
            memcmp(request, kDashDiscoveryRequest, strlen(kDashDiscoveryRequest)) != 0)
            continue;
        (void)sendto(dashDiscoverySocket, kDashDiscoveryResponse,
                     strlen(kDashDiscoveryResponse), 0,
                     reinterpret_cast<const sockaddr *>(&peer), peerLength);
    }
}
#else
static void dashStartDiscovery() {}
static void dashDiscoveryTick() {}
#endif

static void dashApplyWifiSlot(uint8_t slot)
{
    DashWifiGuard guard;
    if (slot >= wifiNetworkCount)
        return;
    const DashWifiNetwork &n = wifiNetworks[slot];
    strlcpy(staSSID, n.ssid, sizeof(staSSID));
    strlcpy(staPass, n.pass, sizeof(staPass));
    staStaticIP = n.useStatic;
    if (n.useStatic)
    {
        staIP.fromString(n.ip);
        staGW.fromString(n.gw);
        staMask.fromString(n.mask);
        staDNS.fromString(n.dns);
    }
    else
    {
        staIP = IPAddress(0, 0, 0, 0);
    }
    wifiActiveSlot = static_cast<int8_t>(slot);
}

static void dashRotateAndConnect()
{
    DashWifiGuard guard;
    if (wifiNetworkCount == 0)
        return;
    uint8_t next = wifiNextRotateSlot % wifiNetworkCount;
    wifiNextRotateSlot = (next + 1) % wifiNetworkCount;
    dashApplyWifiSlot(next);
    dashLog("[WIFI] Trying slot " + String(next) + ": " + String(staSSID));
    dashStartAccessPoint(true);
    dashBeginSTA();
}

static void dashScheduleSTAConnect(unsigned long delayMs)
{
    DashWifiGuard guard;
    if (strlen(staSSID) == 0)
        return;
    staConnectAttemptActive = false;
    staRetryAt = millis() + delayMs;
}

static void dashPrepareWifiScan()
{
    DashWifiGuard guard;
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
}

static void performAutoUpdate(); // forward decl, defined below

static void dashCheckWifi()
{
    bool runAutoUpdate = false;
    {
        DashWifiGuard guard;
        static unsigned long lastCheck = 0;
        if (wifiNetworkCount == 0)
            return;
        unsigned long now = millis();
        if (!staConnected && !staConnectAttemptActive && staRetryAt > 0 && (long)(now - staRetryAt) >= 0)
        {
            staRetryAt = 0;
            dashRotateAndConnect();
        }

        if (now - lastCheck < 5000)
            return;
        lastCheck = now;

        bool connected = WiFi.status() == WL_CONNECTED;
        if (connected != staConnected)
        {
            staConnected = connected;
            if (connected)
            {
                staConnectAttemptActive = false;
                staRetryAt = 0;
                dashLog("[WIFI] Connected to " + String(staSSID) + " IP: " + WiFi.localIP().toString());
                // Schedule auto-update check 15 s after STA comes up (grace period for other boot work)
                if (autoUpdateEnabled && !autoUpdateDone)
                    autoUpdateEligibleAt = millis() + 15000;
            }
            else
            {
                dashLog("[WIFI] Disconnected from " + String(staSSID));
                staConnectAttemptActive = false;
                staRetryAt = now + kDashStaRetryMs;
            }
        }

        if (!connected && staConnectAttemptActive && now - staConnectStartedAt >= kDashStaConnectTimeoutMs)
        {
            staConnectAttemptActive = false;
            WiFi.disconnect(false, false);
            dashStartAccessPoint(false);
            staRetryAt = now + kDashStaRetryMs;
            dashLog("[WIFI] STA connect timed out; keeping AP-only mode");
        }

        // Fire one-shot auto-update check once eligible
        if (autoUpdateEnabled && !autoUpdateDone && staConnected && autoUpdateEligibleAt > 0 &&
            static_cast<long>(millis() - autoUpdateEligibleAt) >= 0)
        {
            autoUpdateDone = true;
            runAutoUpdate = true;
        }
    }
    if (runAutoUpdate)
        performAutoUpdate();
}

static void handleWifiScan()
{
    DashWifiGuard guard;
    dashPrepareWifiScan();
    int n = WiFi.scanNetworks(false, false, false, 300);
    String j = "{\"networks\":[";
    for (int i = 0; i < n && i < 20; i++)
    {
        if (i)
            j += ",";
        j += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i).c_str()) + "\"";
        j += ",\"rssi\":" + String(WiFi.RSSI(i));
        j += ",\"enc\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
        j += ",\"ch\":" + String(WiFi.channel(i));
        j += "}";
    }
    j += "]}";
    WiFi.scanDelete();
    server.send(200, "application/json", j);
}

static void dashPersistWifiSlot(uint8_t slot)
{
    if (slot >= wifiNetworkCount)
        return;
    const DashWifiNetwork &n = wifiNetworks[slot];
    prefs.putString(dashWifiKey(slot, "s").c_str(), String(n.ssid));
    prefs.putString(dashWifiKey(slot, "p").c_str(), String(n.pass));
    prefs.putBool(dashWifiKey(slot, "t").c_str(), n.useStatic);
    if (n.useStatic)
    {
        prefs.putString(dashWifiKey(slot, "i").c_str(), String(n.ip));
        prefs.putString(dashWifiKey(slot, "g").c_str(), String(n.gw));
        prefs.putString(dashWifiKey(slot, "m").c_str(), String(n.mask));
        prefs.putString(dashWifiKey(slot, "d").c_str(), String(n.dns));
    }
    else
    {
        prefs.remove(dashWifiKey(slot, "i").c_str());
        prefs.remove(dashWifiKey(slot, "g").c_str());
        prefs.remove(dashWifiKey(slot, "m").c_str());
        prefs.remove(dashWifiKey(slot, "d").c_str());
    }
}

static void dashRemoveWifiSlotKeys(uint8_t slot)
{
    prefs.remove(dashWifiKey(slot, "s").c_str());
    prefs.remove(dashWifiKey(slot, "p").c_str());
    prefs.remove(dashWifiKey(slot, "t").c_str());
    prefs.remove(dashWifiKey(slot, "i").c_str());
    prefs.remove(dashWifiKey(slot, "g").c_str());
    prefs.remove(dashWifiKey(slot, "m").c_str());
    prefs.remove(dashWifiKey(slot, "d").c_str());
}

// Save to slot N (0..count). idx == count means append (new). Reconnect on save.
static void handleWifiConfig()
{
    DashWifiGuard guard;
    DashPrefsGuard prefsGuard;
    if (!server.hasArg("ssid"))
    {
        server.send(200, "application/json", "{\"ok\":true}");
        return;
    }

    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (!dashStaConfigLengthValid(ssid, pass) || dashStaSsidLooksCorrupt(ssid) || ssid.length() == 0)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid SSID or password\"}");
        return;
    }

    int idx = -1;
    if (server.hasArg("idx"))
    {
        long parsedIdx = -1;
        if (!dashParseLong(server.arg("idx"), parsedIdx) || parsedIdx < -1 || parsedIdx > kDashMaxWifiNetworks)
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid network index\"}");
            return;
        }
        idx = static_cast<int>(parsedIdx);
    }
    if (idx < 0 || idx > wifiNetworkCount)
        idx = wifiNetworkCount; // append

    if (idx == kDashMaxWifiNetworks)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Max networks reached\"}");
        return;
    }

    DashWifiNetwork candidate = {};
    dashClearWifiNetwork(candidate);
    strlcpy(candidate.ssid, ssid.c_str(), sizeof(candidate.ssid));
    strlcpy(candidate.pass, pass.c_str(), sizeof(candidate.pass));
    candidate.useStatic = false;
    if (server.hasArg("static") && !dashParseBool(server.arg("static"), candidate.useStatic))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid static IP flag\"}");
        return;
    }
    if (candidate.useStatic)
    {
        String ip = server.arg("ip");
        String gateway = server.arg("gw");
        String mask = server.arg("mask");
        String dns = server.arg("dns");
        if (!dashStaticIpConfigValid(ip.c_str(), gateway.c_str(), mask.c_str(), dns.c_str()))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid static IP configuration\"}");
            return;
        }
        strlcpy(candidate.ip, ip.c_str(), sizeof(candidate.ip));
        strlcpy(candidate.gw, gateway.c_str(), sizeof(candidate.gw));
        strlcpy(candidate.mask, mask.c_str(), sizeof(candidate.mask));
        strlcpy(candidate.dns, dns.c_str(), sizeof(candidate.dns));
    }
    DashWifiNetwork previous = wifiNetworks[idx];
    uint8_t previousCount = wifiNetworkCount;
    wifiNetworks[idx] = candidate;
    if (idx == wifiNetworkCount)
        wifiNetworkCount++;

    if (!prefs.begin(PREFS_NS, false))
    {
        wifiNetworks[idx] = previous;
        wifiNetworkCount = previousCount;
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS open failed\"}");
        return;
    }
    prefs.putUChar("wn_cnt", wifiNetworkCount);
    dashPersistWifiSlot(idx);
    if (!prefs.end())
    {
        wifiNetworks[idx] = previous;
        wifiNetworkCount = previousCount;
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
        return;
    }

    dashLog("[WIFI] Saved slot " + String(idx) + ": " + ssid);

    // Switch to newly saved slot and connect
    wifiNextRotateSlot = idx;
    dashApplyWifiSlot(idx);
    dashPrepareStaReconnect();

    server.send(200, "application/json", "{\"ok\":true,\"idx\":" + String(idx) + "}");
    dashScheduleSTAConnect(1000);
}

static void handleWifiDelete()
{
    DashWifiGuard guard;
    DashPrefsGuard prefsGuard;
    if (!server.hasArg("idx"))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing idx\"}");
        return;
    }
    long parsedIdx = -1;
    if (!dashParseLong(server.arg("idx"), parsedIdx))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad idx\"}");
        return;
    }
    int idx = static_cast<int>(parsedIdx);
    if (idx < 0 || idx >= wifiNetworkCount)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad idx\"}");
        return;
    }

    String removedSsid = wifiNetworks[idx].ssid;
    DashWifiNetwork previousNetworks[kDashMaxWifiNetworks];
    memcpy(previousNetworks, wifiNetworks, sizeof(wifiNetworks));
    uint8_t previousCount = wifiNetworkCount;
    // Shift slots down
    for (uint8_t i = idx; i + 1 < wifiNetworkCount; i++)
        wifiNetworks[i] = wifiNetworks[i + 1];
    wifiNetworkCount--;
    dashClearWifiNetwork(wifiNetworks[wifiNetworkCount]);

    // Rewrite all slot keys
    if (!prefs.begin(PREFS_NS, false))
    {
        memcpy(wifiNetworks, previousNetworks, sizeof(wifiNetworks));
        wifiNetworkCount = previousCount;
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS open failed\"}");
        return;
    }
    prefs.putUChar("wn_cnt", wifiNetworkCount);
    for (uint8_t i = 0; i < wifiNetworkCount; i++)
        dashPersistWifiSlot(i);
    for (uint8_t i = wifiNetworkCount; i < kDashMaxWifiNetworks; i++)
        dashRemoveWifiSlotKeys(i);
    if (!prefs.end())
    {
        memcpy(wifiNetworks, previousNetworks, sizeof(wifiNetworks));
        wifiNetworkCount = previousCount;
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
        return;
    }

    dashLog("[WIFI] Deleted slot " + String(idx) + ": " + removedSsid);

    // Adjust active slot if needed
    if (wifiActiveSlot == idx)
    {
        wifiActiveSlot = -1;
        if (staConnectAttemptActive || staConnected)
        {
            WiFi.disconnect(false, false);
            staConnectAttemptActive = false;
            staConnected = false;
        }
        if (wifiNetworkCount > 0)
        {
            wifiNextRotateSlot = 0;
            dashRotateAndConnect();
        }
        else
        {
            staSSID[0] = 0;
            staPass[0] = 0;
            dashStartAccessPoint(false);
        }
    }
    else if (wifiActiveSlot > idx)
    {
        wifiActiveSlot--;
    }
    if (wifiNextRotateSlot >= wifiNetworkCount)
        wifiNextRotateSlot = 0;

    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleWifiNetworks()
{
    DashWifiGuard guard;
    String j = "{\"max\":";
    j += kDashMaxWifiNetworks;
    j += ",\"count\":";
    j += wifiNetworkCount;
    j += ",\"active\":";
    j += wifiActiveSlot;
    j += ",\"networks\":[";
    for (uint8_t i = 0; i < wifiNetworkCount; i++)
    {
        if (i)
            j += ",";
        const DashWifiNetwork &n = wifiNetworks[i];
        j += "{\"idx\":";
        j += i;
        j += ",\"ssid\":\"" + jsonEscape(n.ssid) + "\"";
        j += ",\"hasPass\":" + String(strlen(n.pass) > 0 ? "true" : "false");
        j += ",\"static\":" + String(n.useStatic ? "true" : "false");
        if (n.useStatic)
        {
            j += ",\"ip\":\"" + String(n.ip) + "\"";
            j += ",\"gw\":\"" + String(n.gw) + "\"";
            j += ",\"mask\":\"" + String(n.mask) + "\"";
            j += ",\"dns\":\"" + String(n.dns) + "\"";
        }
        j += "}";
    }
    j += "]}";
    server.send(200, "application/json", j);
}

static void handleWifiStatus()
{
    DashWifiGuard guard;
    bool stored = wifiNetworkCount > 0;
    bool connectedNow = WiFi.status() == WL_CONNECTED;
    IPAddress staIp = WiFi.localIP();
    bool connected = connectedNow;
    String activeSsid = connectedNow ? WiFi.SSID() : String(staSSID);
    if (dashStaSsidLooksCorrupt(activeSsid))
        activeSsid = "";
    String j = "{\"connected\":";
    j += connected ? "true" : "false";
    j += ",\"ssid\":\"" + jsonEscape(activeSsid) + "\"";
    j += ",\"stored\":" + String(stored ? "true" : "false");
    j += ",\"count\":" + String(wifiNetworkCount);
    j += ",\"active\":" + String(wifiActiveSlot);
    if (connected)
        j += ",\"ip\":\"" + staIp.toString() + "\"";
    j += ",\"static\":" + String(staStaticIP ? "true" : "false");
    if (staStaticIP)
    {
        j += ",\"cfg_ip\":\"" + staIP.toString() + "\"";
        j += ",\"cfg_gw\":\"" + staGW.toString() + "\"";
        j += ",\"cfg_mask\":\"" + staMask.toString() + "\"";
        j += ",\"cfg_dns\":\"" + staDNS.toString() + "\"";
    }
    if (!connected)
        j += ",\"connecting\":" + String(staConnectAttemptActive ? "true" : "false");
    j += "}";
    server.send(200, "application/json", j);
}

// ── AP Config (hotspot name/password) ───────────────────────────

static void handleCanPins()
{
    Preferences canPrefs;
    bool customized = false;
    int tx = -1, rx = -1;
#if defined(DRIVER_TWAI)
    tx = (int)TWAI_TX_PIN;
    rx = (int)TWAI_RX_PIN;
#endif
    if (canPrefs.begin("can", false))
    {
        int storedTx = canPrefs.getChar("tx", -1);
        int storedRx = canPrefs.getChar("rx", -1);
        canPrefs.end();
        if (storedTx >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(storedTx))
        {
            tx = storedTx;
            customized = true;
        }
        if (storedRx >= 0 && GPIO_IS_VALID_GPIO(storedRx))
        {
            rx = storedRx;
            customized = true;
        }
    }
    String j = "{\"tx\":" + String(tx);
    j += ",\"rx\":" + String(rx);
    j += ",\"customized\":" + String(customized ? "true" : "false");
    j += "}";
    server.send(200, "application/json", j);
}

static void handleCanPinsSave()
{
    long parsedTx = -1, parsedRx = -1;
    if (!dashParseLong(server.arg("tx"), parsedTx) || !dashParseLong(server.arg("rx"), parsedRx) ||
        parsedTx < 0 || parsedTx > 255 || parsedRx < 0 || parsedRx > 255)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid pin value\"}");
        return;
    }
    int tx = static_cast<int>(parsedTx);
    int rx = static_cast<int>(parsedRx);

    if (tx < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(tx) || rx < 0 || !GPIO_IS_VALID_GPIO(rx))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Pin is not valid for this board\"}");
        return;
    }
    if (tx == rx)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"TX and RX must differ\"}");
        return;
    }
    // GPIO 6-11 are reserved for SPI flash on most ESP32 modules
    if (dashCanPinReserved(tx) || dashCanPinReserved(rx))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"GPIO 6-11 reserved for flash\"}");
        return;
    }

    Preferences canPrefs;
    if (!canPrefs.begin("can", false))
    {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS open failed\"}");
        return;
    }
    canPrefs.putChar("tx", (int8_t)tx);
    canPrefs.putChar("rx", (int8_t)rx);
    if (!canPrefs.end())
    {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
        return;
    }

    dashLog("[CAN] Pins saved: TX=" + String(tx) + " RX=" + String(rx) + " (reboot required)");
    server.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
}

// ── Settings Backup / Restore ───────────────────────────────────

static void handleSettingsExport()
{
    if (!server.authenticate(DASH_OTA_USER, DASH_OTA_PASS))
    {
        server.requestAuthentication();
        return;
    }
    DashWifiGuard wifiGuard;
    DashPrefsGuard prefsGuard;
    String apSsid = apSSID, apPassword = apPass;
    bool beta = updateBetaChannel;
    bool apHid = apHidden;
    bool startAfterAp = apInjectionGate;
    bool summonOnly = summonOnlyInjection;
    bool h3Slew = hw3OffsetSlew;
    uint8_t h3SlewRate = hw3SlewRate;
    int canTx = -1, canRx = -1;
    Preferences cp;
    if (cp.begin("can", true))
    {
        canTx = cp.getChar("tx", -1);
        canRx = cp.getChar("rx", -1);
        cp.end();
    }

    String j = "{\"version\":\"" FIRMWARE_VERSION "\"";
    j += ",\"ap\":{\"ssid\":\"" + jsonEscape(apSsid) + "\",\"pass\":\"" + jsonEscape(apPassword) + "\",\"hidden\":" + String(apHid ? "true" : "false") + "}";
    j += ",\"wifi\":{\"networks\":[";
    for (uint8_t i = 0; i < wifiNetworkCount; i++)
    {
        if (i)
            j += ",";
        const DashWifiNetwork &network = wifiNetworks[i];
        j += "{\"ssid\":\"" + jsonEscape(network.ssid) + "\",\"pass\":\"" + jsonEscape(network.pass) + "\"";
        j += ",\"static\":" + String(network.useStatic ? "true" : "false");
        j += ",\"ip\":\"" + jsonEscape(network.ip) + "\",\"gw\":\"" + jsonEscape(network.gw) + "\"";
        j += ",\"mask\":\"" + jsonEscape(network.mask) + "\",\"dns\":\"" + jsonEscape(network.dns) + "\"}";
    }
    j += "]}";
    j += ",\"plugins\":{\"replay\":" + String(pluginGetReplayCount()) +
         ",\"startAfterAp\":" + String(startAfterAp ? "true" : "false") +
         ",\"summonOnly\":" + String(summonOnly ? "true" : "false") + "}";
    j += ",\"hw3\":{\"offsetSlew\":" + String(h3Slew ? "true" : "false") + ",\"slewRate\":" + String(h3SlewRate) + "}";
    j += ",\"dashboard\":{\"hw\":" + String(hwMode) + ",\"can\":" + String(canActive ? "true" : "false");
    j += ",\"nagMode\":" + String(dashNagMode);
    j += ",\"speedAuto\":" + String(dashSpeedProfileAuto ? "true" : "false");
    j += ",\"speedProfile\":" + String(dashManualSpeedProfile);
    j += ",\"ledBrightness\":" + String(dashLedBrightness) + "}";
    j += ",\"can\":{\"tx\":" + String(canTx) + ",\"rx\":" + String(canRx) + "}";
    j += ",\"beta\":" + String(beta ? "true" : "false");
    j += ",\"autoUpdate\":" + String(autoUpdateEnabled ? "true" : "false");
    j += "}";

    server.sendHeader("Content-Disposition", "attachment; filename=\"evtools-backup.json\"");
    server.send(200, "application/json", j);
}

static void handleSettingsImport()
{
    if (!server.authenticate(DASH_OTA_USER, DASH_OTA_PASS))
    {
        server.requestAuthentication();
        return;
    }
    DashPrefsGuard prefsGuard;
    String body = server.arg("plain");
    if (body.length() == 0)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Empty body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    for (const char *section : {"ap", "wifi", "can", "dashboard", "plugins", "hw3"})
    {
        JsonVariant value = doc[section];
        if (!value.isNull() && !value.is<JsonObject>())
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid settings section\"}");
            return;
        }
    }
    auto validOptionalString = [](JsonVariant value) -> bool
    {
        return value.isNull() || value.is<const char *>();
    };

    DashWifiNetwork importedNetworks[kDashMaxWifiNetworks] = {};
    uint8_t importedNetworkCount = 0;
    bool hasWifiImport = false;
    if (doc["wifi"].is<JsonObject>())
    {
        JsonObject wifi = doc["wifi"];
        if (!wifi["networks"].isNull() && !wifi["networks"].is<JsonArray>())
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid WiFi networks\"}");
            return;
        }
        JsonArray networks = wifi["networks"];
        if (networks)
        {
            hasWifiImport = true;
            if (networks.size() > kDashMaxWifiNetworks)
            {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"Too many WiFi networks\"}");
                return;
            }
            for (JsonObject item : networks)
            {
                if (!validOptionalString(item["ssid"]) || !validOptionalString(item["pass"]) ||
                    !validOptionalString(item["ip"]) || !validOptionalString(item["gw"]) ||
                    !validOptionalString(item["mask"]) || !validOptionalString(item["dns"]))
                {
                    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid WiFi network\"}");
                    return;
                }
                String ssid = item["ssid"] | "";
                String pass = item["pass"] | "";
                if (ssid.length() == 0 || !dashStaConfigLengthValid(ssid, pass) || dashStaSsidLooksCorrupt(ssid))
                {
                    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid WiFi network\"}");
                    return;
                }
                DashWifiNetwork &network = importedNetworks[importedNetworkCount++];
                strlcpy(network.ssid, ssid.c_str(), sizeof(network.ssid));
                strlcpy(network.pass, pass.c_str(), sizeof(network.pass));
                JsonVariant staticValue = item["static"];
                if (!staticValue.isNull() && !staticValue.is<bool>())
                {
                    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid static IP flag\"}");
                    return;
                }
                network.useStatic = staticValue.isNull() ? false : staticValue.as<bool>();
                String ip = item["ip"] | "";
                String gateway = item["gw"] | "";
                String mask = item["mask"] | "";
                String dns = item["dns"] | "";
                if (network.useStatic &&
                    !dashStaticIpConfigValid(ip.c_str(), gateway.c_str(), mask.c_str(), dns.c_str()))
                {
                    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid static IP configuration\"}");
                    return;
                }
                strlcpy(network.ip, ip.c_str(), sizeof(network.ip));
                strlcpy(network.gw, gateway.c_str(), sizeof(network.gw));
                strlcpy(network.mask, mask.c_str(), sizeof(network.mask));
                strlcpy(network.dns, dns.c_str(), sizeof(network.dns));
            }
        }
        else if (!wifi["ssid"].isNull())
        {
            hasWifiImport = true;
            if (!validOptionalString(wifi["ssid"]) || !validOptionalString(wifi["pass"]) ||
                !validOptionalString(wifi["ip"]) || !validOptionalString(wifi["gw"]) ||
                !validOptionalString(wifi["mask"]) || !validOptionalString(wifi["dns"]))
            {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid WiFi network\"}");
                return;
            }
            String ssid = wifi["ssid"] | "";
            String pass = wifi["pass"] | "";
            if (ssid.length() > 0)
            {
                if (!dashStaConfigLengthValid(ssid, pass) || dashStaSsidLooksCorrupt(ssid))
                {
                    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid WiFi network\"}");
                    return;
                }
                DashWifiNetwork &network = importedNetworks[0];
                importedNetworkCount = 1;
                strlcpy(network.ssid, ssid.c_str(), sizeof(network.ssid));
                strlcpy(network.pass, pass.c_str(), sizeof(network.pass));
                JsonVariant staticValue = wifi["static"];
                if (!staticValue.isNull() && !staticValue.is<bool>())
                {
                    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid static IP flag\"}");
                    return;
                }
                network.useStatic = staticValue.isNull() ? false : staticValue.as<bool>();
                String ip = wifi["ip"] | "";
                String gateway = wifi["gw"] | "";
                String mask = wifi["mask"] | "";
                String dns = wifi["dns"] | "";
                if (network.useStatic &&
                    !dashStaticIpConfigValid(ip.c_str(), gateway.c_str(), mask.c_str(), dns.c_str()))
                {
                    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid static IP configuration\"}");
                    return;
                }
                strlcpy(network.ip, ip.c_str(), sizeof(network.ip));
                strlcpy(network.gw, gateway.c_str(), sizeof(network.gw));
                strlcpy(network.mask, mask.c_str(), sizeof(network.mask));
                strlcpy(network.dns, dns.c_str(), sizeof(network.dns));
            }
        }
    }

    int importedCanTx = -1;
    int importedCanRx = -1;
    bool hasCustomPins = false;
    if (doc["can"].is<JsonObject>())
    {
        JsonVariant txValue = doc["can"]["tx"];
        JsonVariant rxValue = doc["can"]["rx"];
        if ((!txValue.isNull() && !txValue.is<int>()) || (!rxValue.isNull() && !rxValue.is<int>()))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid CAN pins\"}");
            return;
        }
        importedCanTx = txValue.isNull() ? -1 : txValue.as<int>();
        importedCanRx = rxValue.isNull() ? -1 : rxValue.as<int>();
        hasCustomPins = importedCanTx >= 0 || importedCanRx >= 0;
        if (hasCustomPins &&
            !(importedCanTx >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(importedCanTx) && importedCanRx >= 0 &&
              GPIO_IS_VALID_GPIO(importedCanRx) && importedCanTx != importedCanRx &&
              !dashCanPinReserved(importedCanTx) && !dashCanPinReserved(importedCanRx)))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid CAN pins\"}");
            return;
        }
    }

    if (doc["ap"].is<JsonObject>())
    {
        if (!validOptionalString(doc["ap"]["ssid"]) || !validOptionalString(doc["ap"]["pass"]))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid AP configuration\"}");
            return;
        }
        const char *ssid = doc["ap"]["ssid"] | "";
        const char *password = doc["ap"]["pass"] | "";
        bool bothEmpty = *ssid == '\0' && *password == '\0';
        if (!bothEmpty && !dashApConfigValid(ssid, password))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid AP configuration\"}");
            return;
        }
        if (!doc["ap"]["hidden"].isNull() && !doc["ap"]["hidden"].is<bool>())
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid AP configuration\"}");
            return;
        }
    }

    bool hasDashboardImport = doc["dashboard"].is<JsonObject>();
    int importedHw = hwMode;
    int importedSpeedProfile = dashManualSpeedProfile;
    int importedLedBrightness = dashLedBrightness;
    int importedNagMode = dashNagMode;
    bool importedCanActive = canActive;
    bool importedSpeedAuto = dashSpeedProfileAuto;
    if (hasDashboardImport)
    {
        JsonObject dashboard = doc["dashboard"];
        auto readBool = [&](const char *key, bool &out) -> bool
        {
            JsonVariant value = dashboard[key];
            if (value.isNull())
                return true;
            if (!value.is<bool>())
                return false;
            out = value.as<bool>();
            return true;
        };
        JsonVariant hwValue = dashboard["hw"];
        JsonVariant speedValue = dashboard["speedProfile"];
        JsonVariant ledValue = dashboard["ledBrightness"];
        JsonVariant nagModeValue = dashboard["nagMode"];
        if ((!hwValue.isNull() && !hwValue.is<int>()) ||
            (!speedValue.isNull() && !speedValue.is<int>()) ||
            (!ledValue.isNull() && !ledValue.is<int>()) ||
            (!nagModeValue.isNull() && !nagModeValue.is<int>()) ||
            !readBool("can", importedCanActive) || !readBool("speedAuto", importedSpeedAuto))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid dashboard settings\"}");
            return;
        }
        if (!hwValue.isNull())
            importedHw = hwValue.as<int>();
        if (!speedValue.isNull())
            importedSpeedProfile = speedValue.as<int>();
        if (!ledValue.isNull())
            importedLedBrightness = ledValue.as<int>();
        if (!nagModeValue.isNull())
            importedNagMode = nagModeValue.as<int>();
        if (importedHw < 0 || importedHw > 2 || importedSpeedProfile < 0 ||
            importedSpeedProfile > (importedHw == 2 ? 4 : 2) ||
            importedLedBrightness < 0 || importedLedBrightness > 255 ||
            importedNagMode < static_cast<int>(NagMode::Disabled) ||
            importedNagMode > static_cast<int>(NagMode::ModeC) ||
            !nagModeAllowedForHardware(static_cast<uint8_t>(importedNagMode),
                                       static_cast<uint8_t>(importedHw)))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid dashboard settings\"}");
            return;
        }
    }
    if (!doc["beta"].isNull() && !doc["beta"].is<bool>())
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid beta setting\"}");
        return;
    }
    if (!doc["autoUpdate"].isNull() && !doc["autoUpdate"].is<bool>())
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid auto-update setting\"}");
        return;
    }
    if (doc["plugins"].is<JsonObject>())
    {
        JsonVariant replay = doc["plugins"]["replay"];
        JsonVariant gate = doc["plugins"]["startAfterAp"];
        JsonVariant summonOnly = doc["plugins"]["summonOnly"];
        if ((!replay.isNull() && (!replay.is<int>() || replay.as<int>() < 1 ||
                                  replay.as<int>() > PLUGIN_REPLAY_COUNT_MAX)) ||
            (!gate.isNull() && !gate.is<bool>()) ||
            (!summonOnly.isNull() && !summonOnly.is<bool>()))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid plugin settings\"}");
            return;
        }
    }
    if (doc["hw3"].is<JsonObject>())
    {
        JsonVariant enabled = doc["hw3"]["offsetSlew"];
        JsonVariant rate = doc["hw3"]["slewRate"];
        if ((!enabled.isNull() && !enabled.is<bool>()) ||
            (!rate.isNull() && (!rate.is<int>() || rate.as<int>() < kHw3SlewRateMin ||
                                rate.as<int>() > kHw3SlewRateMax)))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid HW3 settings\"}");
            return;
        }
    }

    Preferences p;
    if (!p.begin(PREFS_NS, false))
    {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS open failed\"}");
        return;
    }

    if (doc["ap"].is<JsonObject>())
    {
        const char *s = doc["ap"]["ssid"] | "";
        const char *pw = doc["ap"]["pass"] | "";
        size_t ssidLen = strlen(s);
        size_t passLen = strlen(pw);
        if (ssidLen > 0)
            p.putString("ap_ssid", s);
        if (passLen > 0)
            p.putString("ap_pass", pw);
        if (doc["ap"]["hidden"].is<bool>())
            p.putBool("ap_hidden", doc["ap"]["hidden"].as<bool>());
    }
    if (hasWifiImport)
    {
        p.putUChar("wn_cnt", importedNetworkCount);
        for (uint8_t i = 0; i < kDashMaxWifiNetworks; i++)
        {
            const String ssidKey = dashWifiKey(i, "s");
            const String passKey = dashWifiKey(i, "p");
            const String staticKey = dashWifiKey(i, "t");
            const String ipKey = dashWifiKey(i, "i");
            const String gwKey = dashWifiKey(i, "g");
            const String maskKey = dashWifiKey(i, "m");
            const String dnsKey = dashWifiKey(i, "d");
            if (i < importedNetworkCount)
            {
                const DashWifiNetwork &network = importedNetworks[i];
                p.putString(ssidKey.c_str(), network.ssid);
                p.putString(passKey.c_str(), network.pass);
                p.putBool(staticKey.c_str(), network.useStatic);
                if (network.useStatic)
                {
                    p.putString(ipKey.c_str(), network.ip);
                    p.putString(gwKey.c_str(), network.gw);
                    p.putString(maskKey.c_str(), network.mask);
                    p.putString(dnsKey.c_str(), network.dns);
                }
                else
                {
                    p.remove(ipKey.c_str());
                    p.remove(gwKey.c_str());
                    p.remove(maskKey.c_str());
                    p.remove(dnsKey.c_str());
                }
            }
            else
            {
                p.remove(ssidKey.c_str());
                p.remove(passKey.c_str());
                p.remove(staticKey.c_str());
                p.remove(ipKey.c_str());
                p.remove(gwKey.c_str());
                p.remove(maskKey.c_str());
                p.remove(dnsKey.c_str());
            }
        }
        p.remove("wifi_ssid");
        p.remove("wifi_pass");
        p.remove("wifi_static");
        p.remove("wifi_ip");
        p.remove("wifi_gw");
        p.remove("wifi_mask");
        p.remove("wifi_dns");
    }
    if (doc["beta"].is<bool>())
        p.putBool("update_beta", doc["beta"].as<bool>());
    if (doc["autoUpdate"].is<bool>())
        p.putBool("auto_upd", doc["autoUpdate"].as<bool>());
    if (hasDashboardImport)
    {
        p.putUChar("hw", static_cast<uint8_t>(importedHw));
        p.putUChar("hw_def", DASH_DEFAULT_HW);
        p.putBool("can", importedCanActive);
        p.putBool("sp_auto", importedSpeedAuto);
        p.putUChar("sp_sel", static_cast<uint8_t>(importedSpeedProfile));
        p.putUChar("led_b", static_cast<uint8_t>(importedLedBrightness));
        p.putUChar("nag_mode", static_cast<uint8_t>(importedNagMode));
    }
    if (doc["plugins"].is<JsonObject>() && doc["plugins"]["replay"].is<int>())
        p.putUChar("plg_rep", pluginClampReplayCount(doc["plugins"]["replay"].as<int>()));
    if (doc["plugins"].is<JsonObject>() && doc["plugins"]["startAfterAp"].is<bool>())
        p.putBool("ap_gate", doc["plugins"]["startAfterAp"].as<bool>());
    if (doc["plugins"].is<JsonObject>() && doc["plugins"]["summonOnly"].is<bool>())
        p.putBool("sum_only", doc["plugins"]["summonOnly"].as<bool>());
    if (doc["hw3"].is<JsonObject>())
    {
        if (doc["hw3"]["offsetSlew"].is<bool>())
            p.putBool("h3_slw", doc["hw3"]["offsetSlew"].as<bool>());
        if (doc["hw3"]["slewRate"].is<int>())
            p.putUChar("h3_srt", dashClampHw3SlewRate(doc["hw3"]["slewRate"].as<int>()));
    }
    if (!p.end())
    {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
        return;
    }

    if (hasCustomPins)
    {
        Preferences cp;
        if (cp.begin("can", false))
        {
            cp.putChar("tx", (int8_t)importedCanTx);
            cp.putChar("rx", (int8_t)importedCanRx);
            if (!cp.end())
            {
                server.send(500, "application/json", "{\"ok\":false,\"error\":\"CAN pin write failed\"}");
                return;
            }
        }
        else
        {
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"CAN pin NVS open failed\"}");
            return;
        }
    }

    dashLog("[BACKUP] Settings imported (reboot required)");
    server.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
}

static void handleApConfig()
{
    DashWifiGuard guard;
    DashPrefsGuard prefsGuard;
    String newSsid = server.arg("ssid");
    String newPass = server.arg("pass");
    bool hasHidden = server.hasArg("hidden");
    bool newHidden = apHidden;
    if (hasHidden && !dashParseBool(server.arg("hidden"), newHidden))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid hidden flag\"}");
        return;
    }

    if (newSsid.length() == 0)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"SSID required\"}");
        return;
    }
    if (newSsid.length() > kDashMaxSsidLen || dashStaSsidLooksCorrupt(newSsid))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid SSID\"}");
        return;
    }
    if (newPass.length() > 0 && !dashApPasswordLengthValid(newPass.length()))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Password must be 8-63 characters\"}");
        return;
    }

    char previousSsid[sizeof(apSSID)];
    char previousPass[sizeof(apPass)];
    strlcpy(previousSsid, apSSID, sizeof(previousSsid));
    strlcpy(previousPass, apPass, sizeof(previousPass));
    bool previousHidden = apHidden;
    strlcpy(apSSID, newSsid.c_str(), sizeof(apSSID));
    if (newPass.length() > 0)
        strlcpy(apPass, newPass.c_str(), sizeof(apPass));
    if (hasHidden)
        apHidden = newHidden;

    if (!prefs.begin(PREFS_NS, false))
    {
        strlcpy(apSSID, previousSsid, sizeof(apSSID));
        strlcpy(apPass, previousPass, sizeof(apPass));
        apHidden = previousHidden;
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS open failed\"}");
        return;
    }
    prefs.putString("ap_ssid", newSsid);
    if (newPass.length() > 0)
        prefs.putString("ap_pass", newPass);
    if (hasHidden)
        prefs.putBool("ap_hidden", newHidden);
    if (!prefs.end())
    {
        strlcpy(apSSID, previousSsid, sizeof(apSSID));
        strlcpy(apPass, previousPass, sizeof(apPass));
        apHidden = previousHidden;
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
        return;
    }

    dashLog("[WIFI] AP config updated: SSID=" + newSsid + (apHidden ? " (hidden)" : ""));
    server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Saved. Reboot to apply new AP settings.\"}");
}

static void handleApStatus()
{
    DashWifiGuard guard;
    Preferences p;
    bool stored = false;
    if (p.begin(PREFS_NS, false))
    {
        stored = p.isKey("ap_ssid") && p.getString("ap_ssid", "").length() > 0;
        p.end();
    }
    String j = "{\"ssid\":\"" + jsonEscape(apSSID) + "\"";
    j += ",\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
    j += ",\"clients\":" + String(WiFi.softAPgetStationNum());
    j += ",\"stored\":" + String(stored ? "true" : "false");
    j += ",\"hidden\":" + String(apHidden ? "true" : "false");
    j += "}";
    server.send(200, "application/json", j);
}

// ── OTA GitHub Update ───────────────────────────────────────────

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

static const char *GITHUB_REPO = "ev-open-can-tools/ev-open-can-tools";

// Map driver type to release artifact filename
static const char *getFirmwareArtifact()
{
#ifdef FIRMWARE_ARTIFACT
    return FIRMWARE_ARTIFACT;
#else
    return "firmware-esp32.bin";
#endif
}

static bool isTrustedFirmwareUrl(const String &url)
{
    String suffix = "/" + String(getFirmwareArtifact());
    return url.startsWith("https://github.com/ev-open-can-tools/ev-open-can-tools/releases/download/") &&
           url.endsWith(suffix.c_str()) && url.indexOf('?') < 0 && url.indexOf('#') < 0;
}

// Parse the release version format into (major, minor, patch, preRank, preNum).
// Pre-release rank: 0 = stable (no suffix, sorts highest among same M.m.p),
//                  1 = -alpha.N, 2 = -beta.N, 3 = -rc.N (higher rank = closer to stable).
static bool parseVersion(const String &v, int &maj, int &min, int &pat, int &preRank, int &preNum)
{
    maj = min = pat = 0;
    preRank = 0;
    preNum = 0;
    String parsedVersion = v;
    int plus = parsedVersion.indexOf('+');
    if (plus >= 0)
    {
        if (plus == 0 || static_cast<size_t>(plus + 1) >= parsedVersion.length())
            return false;
        bool previousDot = true;
        for (size_t p = static_cast<size_t>(plus + 1); p < parsedVersion.length(); p++)
        {
            char c = parsedVersion[p];
            bool validChar = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                             (c >= 'a' && c <= 'z') || c == '-';
            if (c == '.')
            {
                if (previousDot)
                    return false;
                previousDot = true;
            }
            else if (validChar)
                previousDot = false;
            else
                return false;
        }
        if (previousDot)
            return false;
        parsedVersion = parsedVersion.substring(0, static_cast<size_t>(plus));
    }

    size_t i = 0;
    size_t len = parsedVersion.length();
    auto readInt = [&](int &out) -> bool
    {
        int val = 0;
        size_t start = i;
        while (i < len && parsedVersion[i] >= '0' && parsedVersion[i] <= '9')
        {
            if (val > 1000000)
                return false;
            val = val * 10 + (parsedVersion[i] - '0');
            i++;
        }
        if (i == start)
            return false;
        if (i - start > 1 && parsedVersion[start] == '0')
            return false;
        out = val;
        return true;
    };
    if (!readInt(maj) || i >= len || parsedVersion[i++] != '.' || !readInt(min) ||
        i >= len || parsedVersion[i++] != '.' || !readInt(pat))
        return false;
    if (i == len)
        return true;
    if (parsedVersion[i++] != '-')
        return false;

    String label = parsedVersion.substring(i);
    label.toLowerCase();
    int dot = label.indexOf('.');
    if (dot <= 0)
        return false;
    String kind = label.substring(0, static_cast<size_t>(dot));
    if (kind == "alpha")
        preRank = 1;
    else if (kind == "beta")
        preRank = 2;
    else if (kind == "rc")
        preRank = 3;
    else
        return false;

    String number = label.substring(static_cast<size_t>(dot + 1));
    if (number.length() == 0)
        return false;
    if (number.length() > 1 && number[0] == '0')
        return false;
    preNum = 0;
    for (size_t n = 0; n < number.length(); n++)
    {
        if (number[n] < '0' || number[n] > '9' || preNum > 1000000)
            return false;
        preNum = preNum * 10 + (number[n] - '0');
    }
    return true;
}

// Returns true iff `candidate` is strictly newer than `current`.
static bool isVersionNewer(const String &candidate, const String &current)
{
    int cM, cm, cp, cR, cN;
    int uM, um, up, uR, uN;
    if (!parseVersion(candidate, cM, cm, cp, cR, cN) ||
        !parseVersion(current, uM, um, up, uR, uN))
        return false;
    if (cM != uM)
        return cM > uM;
    if (cm != um)
        return cm > um;
    if (cp != up)
        return cp > up;
    // Same M.m.p — stable (rank 0) beats any prerelease (rank 1-3)
    // For two prereleases: higher rank beats lower (rc > beta > alpha)
    int cEff = (cR == 0) ? 1000 : cR; // stable → very high
    int uEff = (uR == 0) ? 1000 : uR;
    if (cEff != uEff)
        return cEff > uEff;
    return cN > uN;
}

static void handleUpdateCheck()
{
    if (!dashStaConnectedSnapshot())
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"WiFi not connected\"}");
        return;
    }

    WiFiClientSecure client;
#ifndef ESP_PLATFORM
    client.setInsecure();
#endif
    HTTPClient http;

    String url;
    if (updateBetaChannel)
        url = "https://api.github.com/repos/" + String(GITHUB_REPO) + "/releases?per_page=1";
    else
        url = "https://api.github.com/repos/" + String(GITHUB_REPO) + "/releases/latest";

    http.begin(client, url);
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("User-Agent", "ESP32-OTA");
    int code = http.GET();

    if (code != 200)
    {
        http.end();
        server.send(502, "application/json", "{\"ok\":false,\"error\":\"GitHub API error " + String(code) + "\"}");
        return;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"JSON parse error\"}");
        return;
    }

    // Find the right release
    JsonObject release;
    if (updateBetaChannel)
    {
        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject r : arr)
        {
            release = r;
            break; // first (newest) release
        }
    }
    else
    {
        release = doc.as<JsonObject>();
    }

    if (release.isNull())
    {
        server.send(404, "application/json", "{\"ok\":false,\"error\":\"No release found\"}");
        return;
    }

    String tagName = release["tag_name"] | "";
    bool prerelease = release["prerelease"] | false;
    String version = tagName;
    if (version.startsWith("v"))
        version = version.substring(1);

    // Find the matching firmware asset
    String downloadUrl = "";
    const char *artifact = getFirmwareArtifact();
    JsonArray assets = release["assets"];
    for (JsonObject asset : assets)
    {
        String name = asset["name"] | "";
        if (name == artifact)
        {
            downloadUrl = String(asset["browser_download_url"] | "");
            break;
        }
    }

    String j = "{\"ok\":true";
    j += ",\"current\":\"" + jsonEscape(FIRMWARE_VERSION) + "\"";
    j += ",\"latest\":\"" + jsonEscape(version.c_str()) + "\"";
    j += ",\"tag\":\"" + jsonEscape(tagName.c_str()) + "\"";
    j += ",\"prerelease\":" + String(prerelease ? "true" : "false");
    j += ",\"artifact\":\"" + jsonEscape(artifact) + "\"";
    j += ",\"url\":\"" + jsonEscape(downloadUrl.c_str()) + "\"";
    bool isNewer = isVersionNewer(version, String(FIRMWARE_VERSION));
    bool trustedAsset = downloadUrl.length() > 0 && isTrustedFirmwareUrl(downloadUrl);
    j += ",\"update\":" + String(isNewer && trustedAsset ? "true" : "false");
    j += ",\"beta\":" + String(updateBetaChannel ? "true" : "false");
    j += "}";
    server.send(200, "application/json", j);
}

static void handleUpdateInstall()
{
    if (!server.authenticate(DASH_OTA_USER, DASH_OTA_PASS))
    {
        server.requestAuthentication();
        return;
    }
    if (!dashStaConnectedSnapshot())
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"WiFi not connected\"}");
        return;
    }

    String url = server.arg("url");
    if (url.length() == 0)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"No URL provided\"}");
        return;
    }
    if (!isTrustedFirmwareUrl(url))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Untrusted firmware URL\"}");
        return;
    }

    dashLog("[OTA] Starting GitHub update from: " + url);

    WiFiClientSecure client;
#ifndef ESP_PLATFORM
    client.setInsecure();
#endif

    // Follow redirects — GitHub release assets redirect to S3
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.begin(client, url);
    http.addHeader("Accept", "application/octet-stream");
    int code = http.GET();

    if (code != 200)
    {
        dashLog("[OTA] Download failed: HTTP " + String(code));
        http.end();
        server.send(502, "application/json", "{\"ok\":false,\"error\":\"Firmware download failed\"}");
        return;
    }

    int contentLength = http.getSize();
    dashLog(contentLength > 0 ? "[OTA] Downloading " + String(contentLength) + " bytes..."
                              : "[OTA] Downloading chunked firmware...");

    dashQuiesceTransmitForOta("web_install");
    DashMaintenanceCleanupGuard cleanup;
    if (!Update.begin(contentLength > 0 ? static_cast<size_t>(contentLength) : UPDATE_SIZE_UNKNOWN))
    {
        dashResumeTransmitAfterOtaFailure();
        dashLog("[OTA] Update.begin failed: " + String(Update.errorString()));
        http.end();
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"OTA initialization failed\"}");
        return;
    }

    WiFiClient *stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();

    if (written == 0 || (contentLength > 0 && written != static_cast<size_t>(contentLength)))
    {
        dashLog("[OTA] Written " + String(written) + " of " + String(contentLength) + " bytes: " + String(Update.errorString()));
        Update.abort();
        dashResumeTransmitAfterOtaFailure();
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"Incomplete firmware download\"}");
        return;
    }

    if (!Update.end(true))
    {
        dashResumeTransmitAfterOtaFailure();
        dashLog("[OTA] Update finalize failed: " + String(Update.errorString()));
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"Firmware validation failed\"}");
        return;
    }

    if (!Update.isFinished())
    {
        dashResumeTransmitAfterOtaFailure();
        dashLog("[OTA] Update not finished");
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"Firmware update did not finish\"}");
        return;
    }

    dashLog("[OTA] Update successful! Rebooting...");
    server.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
    cleanup.keepInhibited();
    delay(1000);
    ESP.restart();
}

// Check GitHub for a newer release and, if found, download + install it.
// Blocking; on success calls ESP.restart() and never returns.
static void performAutoUpdate()
{
    if (!dashStaConnectedSnapshot())
        return;

    dashLog("[AUTO-OTA] Checking for updates...");

    WiFiClientSecure client;
#ifndef ESP_PLATFORM
    client.setInsecure();
#endif
    HTTPClient http;

    String url;
    if (updateBetaChannel)
        url = "https://api.github.com/repos/" + String(GITHUB_REPO) + "/releases?per_page=1";
    else
        url = "https://api.github.com/repos/" + String(GITHUB_REPO) + "/releases/latest";

    http.begin(client, url);
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("User-Agent", "ESP32-OTA");
    int code = http.GET();
    if (code != 200)
    {
        dashLog("[AUTO-OTA] GitHub API error " + String(code));
        http.end();
        return;
    }
    String payload = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload))
    {
        dashLog("[AUTO-OTA] JSON parse error");
        return;
    }

    JsonObject release;
    if (updateBetaChannel)
    {
        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject r : arr)
        {
            release = r;
            break;
        }
    }
    else
    {
        release = doc.as<JsonObject>();
    }
    if (release.isNull())
    {
        dashLog("[AUTO-OTA] No release found");
        return;
    }

    String tagName = release["tag_name"] | "";
    String version = tagName;
    if (version.startsWith("v"))
        version = version.substring(1);
    if (!isVersionNewer(version, String(FIRMWARE_VERSION)))
    {
        dashLog("[AUTO-OTA] No newer release (latest=" + version + ", current=" FIRMWARE_VERSION ")");
        return;
    }

    const char *artifact = getFirmwareArtifact();
    String downloadUrl = "";
    for (JsonObject asset : release["assets"].as<JsonArray>())
    {
        String name = asset["name"] | "";
        if (name == artifact)
        {
            downloadUrl = String(asset["browser_download_url"] | "");
            break;
        }
    }
    if (!downloadUrl.length())
    {
        dashLog("[AUTO-OTA] No matching artifact for this build");
        return;
    }
    if (!isTrustedFirmwareUrl(downloadUrl))
    {
        dashLog("[AUTO-OTA] Release asset URL rejected");
        return;
    }

    dashLog("[AUTO-OTA] Update " + version + " available. Installing...");

    HTTPClient http2;
    http2.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http2.begin(client, downloadUrl);
    http2.addHeader("Accept", "application/octet-stream");
    int code2 = http2.GET();
    if (code2 != 200)
    {
        dashLog("[AUTO-OTA] Download failed: HTTP " + String(code2));
        http2.end();
        return;
    }
    int len = http2.getSize();
    dashQuiesceTransmitForOta("auto_update");
    DashMaintenanceCleanupGuard cleanup;
    if (!Update.begin(len > 0 ? static_cast<size_t>(len) : UPDATE_SIZE_UNKNOWN))
    {
        dashResumeTransmitAfterOtaFailure();
        dashLog("[AUTO-OTA] Update.begin failed: " + String(Update.errorString()));
        http2.end();
        return;
    }
    WiFiClient *stream = http2.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http2.end();
    if (written == 0 || (len > 0 && written != static_cast<size_t>(len)))
    {
        dashLog("[AUTO-OTA] Written " + String(written) + "/" + String(len) + " bytes: " + String(Update.errorString()));
        Update.abort();
        dashResumeTransmitAfterOtaFailure();
        return;
    }
    if (!Update.end(true))
    {
        dashResumeTransmitAfterOtaFailure();
        dashLog("[AUTO-OTA] Finalize failed: " + String(Update.errorString()));
        return;
    }
    dashLog("[AUTO-OTA] Update successful! Rebooting...");
    cleanup.keepInhibited();
    delay(1000);
    ESP.restart();
}

static void handleAutoUpdate()
{
    DashPrefsGuard guard;
    if (server.hasArg("enabled"))
    {
        bool requested = false;
        if (!dashParseBool(server.arg("enabled"), requested))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid auto-update value\"}");
            return;
        }
        bool previous = autoUpdateEnabled;
        autoUpdateEnabled = requested;
        if (!prefs.begin(PREFS_NS, false))
        {
            autoUpdateEnabled = previous;
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS open failed\"}");
            return;
        }
        prefs.putBool("auto_upd", autoUpdateEnabled);
        if (!prefs.end())
        {
            autoUpdateEnabled = previous;
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
            return;
        }
        dashLog("[AUTO-OTA] " + String(autoUpdateEnabled ? "enabled" : "disabled"));
    }
    String j = "{\"ok\":true,\"enabled\":";
    j += autoUpdateEnabled ? "true" : "false";
    j += "}";
    server.send(200, "application/json", j);
}

static void handleUpdateBeta()
{
    DashPrefsGuard guard;
    if (server.hasArg("beta"))
    {
        bool requested = false;
        if (!dashParseBool(server.arg("beta"), requested))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid beta value\"}");
            return;
        }
        bool previous = updateBetaChannel;
        updateBetaChannel = requested;
        if (!prefs.begin(PREFS_NS, false))
        {
            updateBetaChannel = previous;
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS open failed\"}");
            return;
        }
        prefs.putBool("update_beta", updateBetaChannel);
        if (!prefs.end())
        {
            updateBetaChannel = previous;
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS write failed\"}");
            return;
        }
        dashLog("[OTA] Channel: " + String(updateBetaChannel ? "beta" : "stable"));
    }
    String j = "{\"ok\":true,\"beta\":" + String(updateBetaChannel ? "true" : "false");
    j += ",\"version\":\"" + jsonEscape(FIRMWARE_VERSION) + "\"}";
    server.send(200, "application/json", j);
}

// ── Plugin frame callback wrapper ───────────────────────────────

static void dashPluginProcess(const CanFrame &frame, CanDriver &driver)
{
    if (!dashInjectionActive())
        return;
    pluginProcessFrame(frame, driver);
}

// BLE mode deliberately skips the WiFi/HTTP task. Keep recorder deadline,
// CAN-loss detection, and board-local persistence alive without touching WiFi.
static void recorderMaintenanceTask(void *)
{
    for (;;)
    {
        try
        {
            dashEventTick();
        }
        catch (const std::bad_alloc &)
        {
            Serial.println("[ERR] Recorder maintenance out of memory");
        }
        catch (const std::exception &)
        {
            Serial.println("[ERR] Recorder maintenance failed");
        }
        catch (...)
        {
            Serial.println("[ERR] Recorder maintenance failed");
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static void dashStartRecorderMaintenance()
{
#if SOC_CPU_CORES_NUM == 1
    BaseType_t result = xTaskCreate(recorderMaintenanceTask, "recorder", 12288,
                                    nullptr, 1, nullptr);
#else
    BaseType_t result = xTaskCreatePinnedToCore(recorderMaintenanceTask, "recorder", 12288,
                                                nullptr, 1, nullptr, 1);
#endif
    if (result != pdPASS)
        dashLog("[ERR] Recorder maintenance task failed to start");
}

static void dashNagProcess(CanFrame frame, CanDriver &driver)
{
    if (dashNagMode == static_cast<uint8_t>(NagMode::Disabled))
    {
        if (appDashboardDecisionObserver)
            appDashboardDecisionObserver(false, "nag_disabled");
        return;
    }
    dashNagHandler.handleMessageAt(frame, driver, millis(), dashInjectionActive());
}

static void webTask(void *)
{
    for (;;)
    {
        try
        {
#ifdef ESP_PLATFORM
            RuntimeDiagnostics::noteWebLoop();
#else
            ArduinoOTA.handle();
            server.handleClient();
#endif
            dashDiscoveryTick();
            dashCheckWifi();
            dashEventTick();
        }
        catch (const std::bad_alloc &)
        {
            Serial.println("[ERR] Web task out of memory");
        }
        catch (const std::exception &)
        {
            Serial.println("[ERR] Web task request failed");
        }
        catch (...)
        {
            Serial.println("[ERR] Web task request failed");
        }
        // ESP-IDF HTTP server is event-driven in its own task. WiFi rotation,
        // OTA eligibility, and persistence deadlines only need coarse checks.
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static void mcpDashOnActiveSpeedProfile(uint8_t profile)
{
    DashDataGuard guard;
    const uint32_t now = dashHandler ? dashHandler->speedProfileChangeMs : millis();
    dashRecorder.recordSetting(kRecorderSettingSpeedProfile,
                               dashClampSpeedProfileForHw(hwMode, profile), now);
}

static CarManagerBase *handlerPool[3] = {};

static void dashInitHandlers()
{
    static LegacyHandler legacyHandler;
    static HW3Handler hw3Handler;
    static HW4Handler hw4Handler;
    handlerPool[0] = &legacyHandler;
    handlerPool[1] = &hw3Handler;
    handlerPool[2] = &hw4Handler;
    for (int i = 0; i < 3; i++)
    {
        handlerPool[i]->onFrame = mcpDashOnFrame;
        handlerPool[i]->onExperimentalFrame = mcpDashOnExperimentalFrame;
        handlerPool[i]->onSpeedProfileChanged = mcpDashOnActiveSpeedProfile;
    }
    // The active car handler already observes every original RX frame.
    // A second Nag callback would duplicate DAS/steering/0x370 in telemetry/logs.
    dashNagHandler.onFrame = nullptr;
    dashNagHandler.submitTx = dashSubmitNagTx;
}

static void dashSwapHandler(uint8_t mode)
{
    if (mode > 2 || !handlerPool[mode])
        return;
    AppHandlerGuard appGuard;
    CarManagerBase *next = handlerPool[mode];
    CarManagerBase *previous = dashHandler;
    if (previous)
    {
        next->enablePrint = (bool)previous->enablePrint;
        next->APActive = (bool)previous->APActive;
        next->Parked = (bool)previous->Parked;
        next->Summoning = (bool)previous->Summoning;
        next->last921Ms = (uint32_t)previous->last921Ms;
        next->last923Ms = (uint32_t)previous->last923Ms;
        next->last280Ms = (uint32_t)previous->last280Ms;
        next->last390Ms = (uint32_t)previous->last390Ms;
        next->last599Ms = (uint32_t)previous->last599Ms;
        next->last1016Ms = (uint32_t)previous->last1016Ms;
        next->last1021Ms = (uint32_t)previous->last1021Ms;
    }
    next->ADEnabled = false;
    next->gatewayAutopilot = -1;
    next->dasAutopilotStatus = -1;
    next->sprSeen = false;
    next->lastAca = false;
    next->lastSummonActivityMs = 0;
    next->resetSummonOnlyPolicyState();
    next->checkAD = dashCheckADEnabled;
    next->checkNag = dashCheckNagDisabled;
    next->speedProfileAuto = (bool)dashSpeedProfileAuto;
    next->speedProfile = dashClampSpeedProfileForHw(hwMode, dashManualSpeedProfile);
    dashHandler = next;
    dashTelemetry.setLayout(mode == 0 ? Chassis::DasLayout::LegacyHw3
                                     : mode == 2 ? Chassis::DasLayout::StandardHw4
                                                  : Chassis::DasLayout::LegacyHw3);
    dashApplyRuntimeState(false);
    appActiveHandler = next;
    // Handler installation changes the effective profile even when automatic
    // mode selects the same vehicle input. Publish at this boundary so boot
    // and runtime swaps cannot leave a stale recorder snapshot.
    dashRecordEffectiveConfiguration();
#if defined(DASH_RGB_STATUS_LED)
    appRefreshStatusLed();
#endif
    // Preserve plugin acceptance IDs across handler changes.
    dashReapplyFiltersWithPlugins();
    const char *hwName = "LEGACY";
    if (mode == 1)
        hwName = "HW3";
    else if (mode == 2)
        hwName = "HW4";
    dashLog("[CFG] Handler switched to " + String(hwName));
}

static bool dashInitializeGuards()
{
    return DashDataGuard::initialize() && DashWifiGuard::initialize() &&
           DashPrefsGuard::initialize() && PluginLockGuard::initialize();
}

#if defined(DRIVER_ESP32_EXT_MCP2515)
static void mcpDashboardSetup(CarManagerBase *handler, CanDriver *driver, ESP32_MCP2515Driver *mcpDriver)
{
    if (!dashInitializeGuards())
    {
        Serial.println("[ERR] Dashboard mutex allocation failed");
        return;
    }
    dashHandler = handler;
    dashDriver = driver;
    dashMcpDriver = mcpDriver;
#else
static void mcpDashboardSetup(CarManagerBase *handler, CanDriver *driver)
{
    if (!dashInitializeGuards())
    {
        Serial.println("[ERR] Dashboard mutex allocation failed");
        return;
    }
    dashHandler = handler;
    dashDriver = driver;
#endif
    dashTxSessionNonce = esp_random();
    if (dashTxSessionNonce == 0)
        dashTxSessionNonce = 1;
    dashTxScheduler.bind(dashDriver, dashTxPolicyContext);
    dashCanBTxScheduler.bind(dashDriver, dashCanBTxPolicyContext);
    appDashboardTxObserver = mcpDashOnTxFrame;
    appDashboardTxAttemptObserver = mcpDashOnTxAttemptFrame;
    appDashboardDecisionObserver = mcpDashOnInjectionDecision;
    appDashboardMasterTxEnabled = []() { return static_cast<bool>(canActive); };
    appDashboardAnomalyBlocksTx = dashAnomalyBlocksTx;
    appDashboardActivityTxAllowed = dashActivityTxAllowed;
    pluginSetDiagnosticsLogger([](const char *message)
                               { dashLog(String(message)); });
    dashResetWriteProbe();

    const bool spiffsReady = SPIFFS.begin(false);
    if (!spiffsReady)
        dashLog("[WARN] SPIFFS mount failed");
    else
    {
        dashMigrateLegacyIncident();
        dashTrimAckTombstones();
        dashFindLatestIncidentPath();
        dashRefreshIncidentStats();
    }

#ifdef ESP_PLATFORM
    dashRecorder.configure(RuntimeDiagnostics::psramVerified.load(std::memory_order_relaxed),
                            RuntimeDiagnostics::systemInfo.psramBytes);
#else
    dashRecorder.configure(false, 0);
#endif
    // Board-local recording is armed by default; /event_control can disable it.
    dashRecorder.enable(true);

    dashLoadPrefs();
    if (!dashBleMode)
    {
        dashStartAccessPoint(false);
        if (apHidden)
            dashLog("[WIFI] AP SSID is hidden");
        Serial.printf("[WIFI] AP: %s  IP: %s\n", apSSID, WiFi.softAPIP().toString().c_str());
    }
    else
    {
        Serial.println("[MODE] BLE mode: WiFi disabled, radio reserved for BLE");
    }

    dashInitHandlers();
    dashSwapHandler(hwMode);
    dashApplyFilters();

    // Load plugins from SPIFFS
    pluginLoadAll();
    dashRestorePluginStates();
    if (pluginCount > 0)
    {
        dashLog("[PLG] Loaded " + String(pluginCount) + " plugin(s)");
        dashReapplyFiltersWithPlugins();
    }

    // Set plugin processing hook
    appPluginProcess = dashPluginProcess;
    pluginBeforeSend = dashApplyHw3OffsetSlew;

    // Restore dev/test mode (simulated CAN) now the driver + handler are ready.
    dashApplyDevMode(dashDevMode);

    // BLE mode: the core (handlers, plugins, dev mode) is up; skip all WiFi/HTTP
    // bring-up so the radio stays free for BLE (started from main after this).
    if (dashBleMode)
    {
        dashStartRecorderMaintenance();
        dashLog("[BOOT] ev-open-can-tools ready (BLE mode)");
        return;
    }

    ArduinoOTA.setHostname("ev-open-can-tools");
    ArduinoOTA.setPassword(DASH_OTA_PASS);
    ArduinoOTA.onStart([]()
                       {
                           dashQuiesceTransmitForOta("arduino_ota");
                           dashLog("[OTA] Starting...");
                       });
    ArduinoOTA.onEnd([]()
                     { dashLog("[OTA] Done -- rebooting"); });
    ArduinoOTA.onError([](ota_error_t e)
                       {
                           dashResumeTransmitAfterOtaFailure();
                           dashLog("[OTA] Error: " + String(e));
                       });
    ArduinoOTA.begin();

    server.on("/", HTTP_GET, handleRoot);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/dev_mode", HTTP_GET, handleDevMode);
    server.on("/dev_mode", HTTP_POST, handleDevMode);
    server.on("/ble_mode", HTTP_GET, handleBleMode);
    server.on("/ble_mode", HTTP_POST, handleBleMode);
    server.on("/ble_pin", HTTP_GET, handleBlePin);
    server.on("/ble_pin", HTTP_POST, handleBlePin);
#ifdef ESP_PLATFORM
    server.on("/support", HTTP_GET, handleSupport);
#endif
    server.on("/config", HTTP_GET, handleConfigGet);
    server.on("/config", HTTP_POST, handleConfig);
    server.on("/led_brightness", HTTP_POST, handleLedBrightness);
    server.on("/disable", HTTP_POST, handleDisable);
    server.on("/reboot", HTTP_POST, handleReboot);
#ifdef ESP_PLATFORM
    server.on("/gvret/status", HTTP_GET, handleGvretStatus);
    server.on("/gvret/start", HTTP_POST, handleGvretStart);
    server.on("/gvret/stop", HTTP_POST, handleGvretStop);
#endif
    server.on("/update", HTTP_POST, handleOtaResult, handleOtaUpload);
    server.on("/diagnostics_detail", HTTP_GET, handleDiagnosticDetails);
    server.on("/can_self_test", HTTP_POST, handleCanSelfTest);
    server.on("/event_control", HTTP_POST, handleEventControl);
    server.on("/event_list", HTTP_GET, handleEventList);
    server.on("/event_download", HTTP_GET, handleEventDownload);
    server.on("/event_ack", HTTP_POST, handleEventAck);
    server.on("/plugins", HTTP_GET, handlePluginList);
    server.on("/plugin_upload", HTTP_POST, handlePluginUpload);
    server.on("/plugin_install", HTTP_POST, handlePluginInstallFromUrl);
    server.on("/plugin_toggle", HTTP_POST, handlePluginToggle);
    server.on("/plugin_remove", HTTP_POST, handlePluginRemove);
    server.on("/plugin_priority", HTTP_POST, handlePluginPriority);
    server.on("/ap_config", HTTP_POST, handleApConfig);
    server.on("/ap_status", HTTP_GET, handleApStatus);
    server.on("/can_pins", HTTP_GET, handleCanPins);
    server.on("/can_pins", HTTP_POST, handleCanPinsSave);
    server.on("/settings_export", HTTP_GET, handleSettingsExport);
    server.on("/settings_import", HTTP_POST, handleSettingsImport);
    server.on("/wifi_scan", HTTP_GET, handleWifiScan);
    server.on("/wifi_config", HTTP_POST, handleWifiConfig);
    server.on("/wifi_status", HTTP_GET, handleWifiStatus);
    server.on("/wifi_networks", HTTP_GET, handleWifiNetworks);
    server.on("/wifi_delete", HTTP_POST, handleWifiDelete);
    server.on("/update_check", HTTP_GET, handleUpdateCheck);
    server.on("/update_install", HTTP_POST, handleUpdateInstall);
    server.on("/update_beta", HTTP_GET, handleUpdateBeta);
    server.on("/update_beta", HTTP_POST, handleUpdateBeta);
    server.on("/auto_update", HTTP_GET, handleAutoUpdate);
    server.on("/auto_update", HTTP_POST, handleAutoUpdate);

    server.begin();
    dashStartDiscovery();
    if (!server.started())
        dashLog("[ERR] Dashboard HTTP server failed to start");
    if (strlen(staSSID) > 0)
        dashScheduleSTAConnect(kDashStaBootDelayMs);
#if SOC_CPU_CORES_NUM == 1
    BaseType_t webTaskResult = xTaskCreate(webTask, "web", 12288, nullptr, 1, &webTaskHandle);
#else
    BaseType_t webTaskResult =
        xTaskCreatePinnedToCore(webTask, "web", 12288, nullptr, 1, &webTaskHandle, 1);
#endif
    if (webTaskResult != pdPASS)
        dashLog("[ERR] Web maintenance task failed to start");
    Serial.println("[WEB] Dashboard: http://" + WiFi.softAPIP().toString());
    dashLog("[BOOT] Dashboard online; CAN initialization may still be pending");
}

static void mcpDashboardLoop()
{
    if (Update.isRunning())
        return;
    dashFlushPluginStatesIfDue();
    dashRefreshSummonOnlyPolicy();
    if (dashInjectionActive() && dashDriver)
        pluginEmitPeriodicTick(*dashDriver, millis());
    dashCheckBusHealth();
    bool wentOffline = false;
    {
        DashDataGuard guard;
        if (canOnline && Chassis::EventRecorder::postDeadlineReached(millis(), lastFrameMs))
        {
            canOnline = false;
            wentOffline = true;
        }
    }
    if (wentOffline)
    {
        dashLog("[CAN] Bus OFFLINE (timeout)");
    }
#if defined(ESP_PLATFORM)
    if (dashDevMode)
    {
        static unsigned long devHeartbeatMs = 0;
        unsigned long nowHb = millis();
        if (nowHb - devHeartbeatMs >= 2000)
        {
            devHeartbeatMs = nowHb;
            Serial.printf("[DEV] sim traffic: canFrames=%lu inject=%d\n",
                          (unsigned long)RuntimeDiagnostics::canFrames.load(std::memory_order_relaxed),
                          (int)dashInjectionActive());
        }
    }
#endif
#if defined(DASH_RGB_STATUS_LED)
    appRefreshStatusLed(false);
#endif
}

#endif
