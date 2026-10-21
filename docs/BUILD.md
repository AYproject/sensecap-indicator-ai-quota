# Build and flash

## Tested dependency versions

| Dependency | Version |
|---|---|
| ESP32 Arduino core | 3.3.0 |
| ESP32 SDK libraries supplied by core | `idf-release_v5.5-b66b5448-v1` |
| D1 Arduino_GFX subset | 1.6.5, included under `libraries/D1_GFX` |
| PCA95x5 | 0.1.3 |
| TouchLib | 0.0.2 |
| Python | 3.10+; local validation used 3.12 |

## Install Arduino CLI dependencies

Install the official [Arduino CLI](https://arduino.github.io/arduino-cli/), then:

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.0 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "PCA95x5@0.1.3" "TouchLib@0.0.2"
```

The D1-specific graphics library is included. Do not substitute a different Arduino_GFX version while reproducing this release.

Windows PowerShell:

```powershell
.\tools\build.ps1
```

The script finds `arduino-cli` on PATH, then checks the standard Arduino IDE installation. You can explicitly pass `-ArduinoCli <path>`. It uses a temporary ASCII build directory to avoid linker issues with non-ASCII project paths. Use `-OutputDirectory <folder>` to preserve the supplied images during a verification build.

Linux/macOS:

```sh
sh tools/build.sh
```

Equivalent board settings:

```text
ESP32S3 Dev Module
PSRAM: OPI PSRAM
Flash size: 8 MB
Partition scheme: default_8MB
USB CDC on boot: default
USB mode: hwcdc
```

Firmware lives in `firmware/D1_AI_Quota`, with entry points in `src/app.cpp`. Output goes to `prebuilt/`. After intentionally rebuilding those images, regenerate and verify the checksums:

```sh
python tools/check_firmware.py --write
python tools/check_firmware.py
```

The checked-in image manifest describes the public v0.5.2 build. The scripts remap source paths so personal build directories are not embedded in assertion strings. A local rebuild may differ because of tool versions or build metadata; regenerate the manifest only for images you have intentionally built.

## Back up and flash

Install `requirements-dev.txt`, stop any running bridge and close serial monitors. List ports:

```sh
python -m serial.tools.list_ports -v
```

Identify the **CH340, VID 1A86 / PID 7523**. Do not select the RP2040 USB device. For example, with the ESP32-S3 CH340 at COM10:

```sh
python -m esptool --chip esp32s3 --port COM10 read-flash 0x0 0x800000 original-d1-backup.bin
python tools/flash.py --port COM10
```

Keep the backup private: full flash may include settings from whatever was previously installed. Store it outside this repository or in an ignored `private_backup/` directory.

`tools/flash.py` writes the merged 8 MB image at address `0x0`, checks the image hash and requires a CH340 port. It is intentionally an explicit flash command: a CH340 VID/PID alone cannot prove that a different connected board is a D1, so physically identify the cable/device first. Esptool verifies the ESP32-S3 chip type.

For a D1 already using this project's matching bootloader/partition layout, `--app-only` writes just the application at `0x10000`. Use the full image when installing from an unrelated firmware. Lower baud with `--baud 115200` if needed.

This project does not program the RP2040.

## Regenerate UI assets

Generated headers are included, so normal firmware compilation needs no Python asset generation.

```sh
python -m pip install -r requirements-dev.txt
python tools/prepare_assets.py
python tools/prepare_glass.py
```

The fonts are renamed derivatives of Bitstream Vera and retain `FONT_LICENSE.txt`. Glass textures are deterministic procedural images. Blur is baked into static materials, rather than calculated on every device frame.

## Upstream source and license references

The root MIT license does not relicense linked dependencies. Original application sources and build instructions are included so you can rebuild with modified dependencies. Exact upstream sources:

- [ESP32 Arduino core 3.3.0](https://github.com/espressif/arduino-esp32/tree/3.3.0), with its LGPL notice under `prebuilt/licenses/`.
- [ESP32 Arduino SDK libraries](https://github.com/espressif/esp32-arduino-lib-builder/releases/tag/idf-release_v5.5-b66b5448-v1), as selected by the core package.
- [ESP-IDF v5.5](https://github.com/espressif/esp-idf/tree/v5.5), with its Apache license and component copyright inventory under `prebuilt/licenses/`.
- [Arduino_GFX v1.6.5](https://github.com/moononournation/Arduino_GFX/tree/v1.6.5); the used subset is included in this repository.
- TouchLib and PCA95x5 versioned library releases are identified in the table above and installed by Arduino CLI.
