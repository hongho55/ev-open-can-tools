# T2CAN Sentinel

[Documentation](docs/) · [Project lineage](docs/t2can-project-lineage.md) · [Build from source](docs/building.md) · [CAN safety](docs/nag-killer.md)

Safety-gated experimental firmware for LilyGo T-2CAN, currently focused on
Model Y Juniper HW4. It combines the broader
[`ev-open-can-tools`](https://github.com/ev-open-can-tools/ev-open-can-tools)
firmware architecture with source-verified behavior from
[`t2can-roaming`](https://github.com/anoblekman/t2can-roaming). See the
[project lineage](docs/t2can-project-lineage.md) before comparing branches or
vehicle reports.

## Read this first

This project is **not plug-and-play**. CAN connects vehicle computers, including safety-critical systems. A wrong wire, bitrate, filter, checksum, or injected byte can cause dangerous behavior or damage.

Use an isolated bench harness or listen-only tool first. Do not begin on a public road. You are responsible for hardware, testing, local law, warranty, and the result of every frame sent.

New to the project? Follow this order:

1. Start the [guided newcomer onboarding](onboarding/) to choose a goal, vehicle mode, board, installation approach, and matching source-build path.
2. Read [CAN safety and testing](docs/nag-killer.md).
3. For LILYGO T-2CAN, use the board-specific [Sentinel release](https://github.com/hongho55/ev-open-can-tools/releases); for every other board, follow [Build from source](docs/building.md). Never install an upstream EV Open CAN artifact as Sentinel firmware.
4. Keep injection stopped while learning the [Dashboard](docs/dashboard.md).
5. Read the [Plugin system](docs/plugins.md) before installing a transmit rule.

## What it does

Depending on board and build, firmware can:

- observe selected CAN frames;
- show CAN health and freshness in a local dashboard;
- store dashboard and plugin settings;
- provide read-only GVRET/SavvyCAN logging;
- load reviewed JSON plugin rules at runtime;
- transmit only when the configured runtime gates permit it.

Dashboard builds are the easiest starting point. Built-in handlers provide observation; enabled plugins are the automatic injection path.

## Supported boards

| PlatformIO environment | Board | CAN | Dashboard |
| --- | --- | --- | --- |
| `esp32_twai` | Generic ESP32 Dev Module | TWAI | Yes |
| `esp32s2_twai` | ESP32-S2 Saola | TWAI | Yes |
| `esp32c6_twai` | ESP32-C6 DevKitC-1 | TWAI | Yes |
| `lilygo_tcan485_hw3` | LILYGO TCAN485 | TWAI | Yes |
| `lilygo_t2can` | LILYGO T-2CAN | SPI MCP2515 (CAN A / Party CAN) | Yes |
| `m5stack-atomic-can-base` | M5Stack Atom CAN Base | TWAI | Yes |
| `m5stack-atoms3-mini-can-base` | M5Stack AtomS3 Mini CAN Base | TWAI | Yes |
| `esp32_feather_v2_mcp2515` | Feather ESP32 V2 + MCP2515 | SPI MCP2515 | Yes |
| `esp32_ext_mcp2515` | ESP32-S3 + MCP2515 | SPI MCP2515 | Yes |
| `waveshare_ESP32_S3_RS485_CAN` | Waveshare ESP32-S3 RS485/CAN | TWAI | Yes |
| `feather_rp2040_can` | Feather RP2040 CAN | MCP2515 | No |
| `feather_m4_can` | Feather M4 CAN Express | Native CAN | No |

## Quick technical start

Install PlatformIO, then from the repository root:

```bash
cp platformio_profile.example.h platformio_profile.h
pio run -e esp32_ext_mcp2515
pio run -e esp32_ext_mcp2515 -t upload
```

Replace the environment with the exact board. Configure the local profile before building. Never commit or share `platformio_profile.h`; it can contain credentials and keys.

After flashing an ESP32 dashboard board, connect to its hotspot and open `http://192.168.4.1/`. Read the [first-boot steps](docs/building.md#first-boot).

## Documentation

- [Guided newcomer onboarding](onboarding/)
- [Documentation index](docs/)
- [Build from source](docs/building.md)
- [Dashboard](docs/dashboard.md)
- [Plugin system](docs/plugins.md)
- [CAN safety and hands-on-wheel experiments](docs/nag-killer.md)
- [T-2CAN Active FSD integration roadmap](docs/t2can-active-fsd-roadmap.md)
- [ESP32 runtime optimization](docs/esp32-optimization.md)
- [Release notes](CHANGELOG.md)

## Contributing

Useful contributions include sanitized CAN captures, board wiring notes, regression tests, documentation improvements, and carefully reviewed code. When reporting a problem, include board/environment, firmware version, bus/bitrate, exact IDs and DLCs, timestamps, Support diagnostics, and the smallest sanitized capture that demonstrates it. Remove credentials, keys, VINs, and private payloads.

## License and dependencies

GPL-3.0. Main dependencies include [ESP-IDF](https://github.com/espressif/esp-idf), [ArduinoJson](https://github.com/bblanchon/ArduinoJson), [arduino-mcp2515](https://github.com/autowp/arduino-mcp2515), and [Adafruit_CAN](https://github.com/adafruit/Adafruit_CAN). See `THIRD_PARTY_LICENSES` for full notices.

Version is tracked in [`VERSION`](VERSION); release notes are in [`CHANGELOG.md`](CHANGELOG.md).
