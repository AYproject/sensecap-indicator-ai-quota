# Validation scope

## Hardware baseline

The **v0.5.2 application/display implementation** was tested on a physical SenseCAP Indicator D1 (8 MB flash / OPI PSRAM ESP32-S3) under Windows on **2026-10-20**. The public images are rebuilt from those unchanged firmware sources after layout reorganization, with compiler paths anonymized. The exact rebuilt public binary has not been reflashed onto that device. The actual account payloads, USB identifiers, original full-flash backup and local logs are intentionally excluded from this repository.

Verified on that D1:

- Online Codex quota equals the selected display value.
- CH340 transport, expected firmware handshake and automatic reconnect after unplug/replug.
- Window switching, photo freeze/resume and redundant LCD-write suppression.
- Stale, failed, syncing and expired-window values are hidden; a window crossing its reset redraws without another PC packet.
- Failed query retries and restores live data without restarting the bridge.
- Ordinary Windows double-click startup and a second launch requesting a fresh online read.
- Default two-minute idle configuration; accelerated timer test through the same idle path.
- Actual backlight GPIO off, continued acknowledged USB reception while off, no automatic wake from incoming data and no periodic repaint while off.
- Wake composes current/expired values before enabling the backlight.
- Photo mode can sleep and retains its explicit frozen state when woken.
- **Physical finger wake**, including a two-second press: no window change, refresh request or unintended photo-mode toggle. The person at the device confirmed normal behavior.

The two-minute default was verified in firmware state; the automated timeout regression temporarily used a shorter interval, then restored 120 seconds. This is not a measurement of whole-device deep-sleep power consumption; the MCU and touch controller remain awake.

## Public project checks

- Thirty account-independent unit tests cover quota mapping, freshness, retry behavior, managed account initialization and USB identity selection.
- The public checkout is tested with its own Python virtual environment and declared dependencies.
- A separate firmware compilation checks the reorganized source/library layout; application/display logic matches the device-validated sources, with Git normalizing text line endings. Public binaries use path-remapping compiler flags; they have new checksums and are not claimed to be the identical original flashed artifact.
- CI runs account-independent tests, CLI import/help and image checksum validation on Windows/Linux with Python 3.10/3.12. CI has no account credentials or hardware.

The polished mockup in README uses invented values and is not evidence of a new physical measurement.

## Not validated

- Linux/macOS physical USB operation and hidden background launch (Windows scripts only).
- Real-account Volcengine Agent Plan and OpenRouter operation.
- Wi-Fi-only or computer-free quota queries.
- ChatGPT web model message limits or OpenAI API billing.
- Devices other than the SenseCAP Indicator D1.

For changes, describe which checks you actually reran. Do not infer physical touchscreen or optical display behavior from a unit-test result alone.
