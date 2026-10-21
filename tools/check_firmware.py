"""Verify published image checksums, or explicitly refresh them after your own build."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--write', action='store_true')
args = parser.parse_args()
images = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((ROOT / 'prebuilt').glob('*.bin'))}
manifest = ROOT / 'prebuilt/SHA256SUMS.json'
if not images: raise SystemExit('No firmware images found.')
if args.write:
    manifest.write_text(json.dumps(images, indent=2) + '\n', encoding='utf-8')
elif images != json.loads(manifest.read_text(encoding='utf-8')):
    raise SystemExit('Firmware checksum verification failed.')
print(f'{len(images)} firmware image checksums verified.' if not args.write else 'Firmware checksum manifest updated.')
