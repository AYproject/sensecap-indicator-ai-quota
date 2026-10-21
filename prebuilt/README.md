# Prebuilt firmware — v0.5.2

These images are rebuilt for the public source release from the v0.5.2 application/display sources validated on a physical D1 on 2026-10-20. Source layout was reorganized and compiler paths anonymized. Application logic is unchanged; the exact public binary has not been reflashed onto that device. See [validation scope](../docs/VALIDATION.md).

| File | Address | Use |
|---|---|---|
| `D1_AI_Quota.ino.merged.bin` | `0x0` | Full 8 MB image; replaces existing contents |
| `D1_AI_Quota.ino.bin` | `0x10000` | Application update only, matching partition layout required |
| `D1_AI_Quota.ino.bootloader.bin` | `0x0` | Individual bootloader component |
| `D1_AI_Quota.ino.partitions.bin` | `0x8000` | Individual partition table component |

Use `tools/flash.py` for the complete image rather than manually guessing component offsets. Keep the accompanying `licenses/` notices with redistributed binaries. Original application sources, dependency source references and rebuild instructions are provided in [BUILD.md](../docs/BUILD.md).

```sh
python tools/check_firmware.py
```

These files are produced by the build tools, not a dump of a configured device. Account snapshots, token files, personal device backup and NVS data from a user's board are not included.
