"""Explicit ESP32-S3 flashing; never selects the D1's RP2040 USB port."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='ESP32-S3 CH340 port, e.g. COM10 or /dev/ttyUSB0')
    parser.add_argument('--baud', type=int, default=460800)
    parser.add_argument('--app-only', action='store_true', help='Update application at 0x10000; requires the matching existing partition layout')
    args = parser.parse_args()
    from serial.tools import list_ports
    matches = [p for p in list_ports.comports() if p.device == args.port]
    if not matches or (matches[0].vid, matches[0].pid) != (0x1a86, 0x7523):
        parser.error('Select the D1 CH340 USB-UART port (VID 1A86/PID 7523), not its RP2040 USB port.')
    name = 'D1_AI_Quota.ino.bin' if args.app_only else 'D1_AI_Quota.ino.merged.bin'
    image = ROOT / 'prebuilt' / name
    if not image.exists(): parser.error('Build firmware first or use a checkout containing prebuilt images.')
    manifest = ROOT / 'prebuilt/SHA256SUMS.json'
    if not manifest.exists(): parser.error('Missing firmware checksum manifest; run tools/check_firmware.py --write after a local build.')
    checks = json.loads(manifest.read_text(encoding='utf-8'))
    if checks.get(name) != hashlib.sha256(image.read_bytes()).hexdigest():
        parser.error('Firmware checksum mismatch; for your own build run tools/check_firmware.py --write.')
    command = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '--port', args.port,
               '--baud', str(args.baud), 'write-flash', '0x10000' if args.app_only else '0x0', str(image)]
    print('Flashing the D1 ESP32-S3. The full image replaces existing flash contents; back up first.')
    return subprocess.call(command)


if __name__ == '__main__':
    raise SystemExit(main())
