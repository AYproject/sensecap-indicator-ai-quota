# Troubleshooting

| Symptom | Check |
|---|---|
| Screen is dark after a while | It sleeps after two minutes. Touch once and release to wake; the next touch performs an action. |
| `Waiting for connection` | Keep the ESP32-S3 USB-UART connected and start the bridge. A power-only USB cable cannot carry quota updates. |
| `Checking online quota` | Wait for the first successful online read. Cached percentages are deliberately hidden. |
| `Account sign-in required` | Sign in to the official Codex CLI with a ChatGPT account on the same computer/user. An API key login does not expose subscription quota. |
| `Update failed / retrying` | Check internet access and `private/health.json` locally. The reader retries automatically. Do not upload unsanitized runtime files. |
| Percentage differs from ChatGPT web chat | This monitor shows the Codex bucket, not model-specific ChatGPT chat allowances. Compare the same account, window and remaining-vs-used measure. |
| Number changes later than expected | Queries normally run every five minutes; the service may also update its counters on a delay. Tap Refresh for another online snapshot. |
| Percentage is `--` | Unknown/error/stale data and expired windows are masked. This does not mean zero allowance. |
| Cannot find Codex | Install the official CLI and sign in; run from a terminal where `codex` works, or set `D1_CODEX_EXE` to its executable. Windows desktop installations are also discovered automatically. |
| No D1 port found | Check the CH340 driver/cable, USB port permissions on Linux, and that only one unpaired CH340 is connected. Discovery will not pick an ambiguous device. |
| Port busy / flashing fails | Stop that checkout's bridge, close other serial monitors, select CH340 rather than RP2040 and retry at 115200 baud. |
| Replug does not reconnect | Keep the PC bridge running. If several CH340 boards were rearranged, stop the bridge, move the local `private/device_identity.json` aside, and pair with only the D1 connected. |
| Double-click opens then closes | Follow README's virtual environment setup. Run `Start-Demo.ps1` in PowerShell to see installation errors, or use the foreground Python command. |
| Screen remains in Photo mode | Tap again after the wake gesture. Photo freeze is intentional and does not stop incoming data. |
| After computer restart there is no sync | Double-click Start-Demo.cmd again; the project does not install automatic startup. |

The foreground command is the simplest way to see connection/retry events:

```sh
python bridge/monitor.py --source codex --interval 300
```

Before filing an issue, include OS, Python version, firmware version, provider and the fixed error reason from `health.json`. Replace account percentages, timestamps, USB locations and any identifying data with synthetic examples.
