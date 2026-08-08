#!/usr/bin/env bash
set -euo pipefail

# Reuse the G2a drawing workload on a PC-9821.  The automatic display HAL
# must select Core-Graph and expose its 640x480 Cirrus surface.
repo="$(cd "$(dirname "$0")/.." && pwd)"
BOOT98_BEUI_MACHINE=pc9821 \
BOOT98_BEUI_EXPECT_HEIGHT=480 \
BOOT98_BEUI_BACKEND_NAME=Core-Graph/Cirrus \
BOOT98_BEUI_TEST_TAG=cirrus \
	"$repo/scripts/test-boot98-beui-gdc.sh"
