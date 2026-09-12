# T2CAN Gateway for S26

This is the first Android store-and-forward worker for the read-only ESP32
incident recorder.

## Runtime path

```text
UDP discovery
  -> authenticated ESP32 /event_list
  -> authenticated /event_download to app-private .part
  -> size/SHA-256/ETag verification and atomic commit
  -> SQLite QUEUED_LOCAL
  -> HTTPS POST /v1/incidents to the Mac receiver
  -> persist stored/already_stored as ACK_PENDING
  -> authenticated ESP32 /event_ack on a later local session
  -> ACKED
```

The application never sends CAN frames, changes DAS layouts, generates TX rules,
or performs OTA. Discovery is unauthenticated but bounded and read-only; the
ESP32 HTTP endpoints still require the configured read-only recorder credential.

## Build

From this directory, using the repository's local JDK 17 and Android SDK:

```bash
export JAVA_HOME=/opt/homebrew/opt/openjdk@17
export ANDROID_SDK_ROOT="$HOME/Library/Android/sdk"
./gradlew --no-daemon testDebugUnitTest assembleDebug
```

The debug APK is produced under `app/build/outputs/apk/debug/`.

## Private settings

The activity accepts:

- ESP32 read-only username and password;
- the complete Mac HTTPS upload endpoint, normally ending in `/v1/incidents`;
- the Mac receiver bearer token.

Passwords and tokens are stored through Android Keystore-backed AES-GCM and are
never rendered back into the form or written to logs. The upload URL must use
HTTPS, contain no URI credentials, query, or fragment. The receiver remains
loopback-only on the Mac; Tailscale Serve is the intended private ingress.

## Current verification boundary

Pure JVM tests cover discovery, event-list parsing, response contract checks, and
safe handling of missing integrity metadata. Repository tests cover the Python
receiver/collector/queue contracts. A real S26 installation, hotspot automation,
Android network handoff, ESP32 packet exchange, and Tailscale Serve path still
require a physical-device canary before unattended vehicle use.
