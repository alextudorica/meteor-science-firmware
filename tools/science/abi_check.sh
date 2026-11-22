#!/bin/sh
# Check src/hal/hisi/v3_science.h against the Hi3516CV300 vendor headers.
#
# Usage: CC=<arm cross gcc> tools/science/abi_check.sh <vendor include dir>
# The include directory is the one holding hi_comm_isp.h, hi_comm_venc.h and
# hi_comm_vpss.h for hi3516cv300 (for example kernel/include/hi3516cv300 in
# OpenIPC's openhisilicon repository). The headers are only read, never copied.

set -eu

if [ $# -ne 1 ] || [ ! -f "$1/hi_comm_isp.h" ]; then
    echo "usage: CC=<cross gcc> $0 <hi3516cv300 include dir>" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/../.." && pwd)
cc=${CC:-arm-openipc-linux-musleabi-gcc}

"$cc" -std=gnu11 -O2 -c -o /dev/null \
    -I "$1" -I "$root/src/hal/hisi" \
    "$root/tools/science/abi_check.c"

echo "ABI check passed ($("$cc" -dumpmachine))"
