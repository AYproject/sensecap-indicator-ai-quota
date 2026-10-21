#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
arduino-cli compile \
  --fqbn 'esp32:esp32:esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=default,USBMode=hwcdc' \
  --build-property "compiler.c.extra_flags=\"-ffile-prefix-map=$HOME=/builder\" \"-ffile-prefix-map=$ROOT=/source\"" \
  --build-property "compiler.cpp.extra_flags=\"-ffile-prefix-map=$HOME=/builder\" \"-ffile-prefix-map=$ROOT=/source\"" \
  --library "$ROOT/libraries/D1_GFX" \
  --output-dir "$ROOT/prebuilt" \
  "$ROOT/firmware/D1_AI_Quota"
