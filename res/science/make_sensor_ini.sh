#!/bin/sh
# Derive the sensor file divinus reads from the IMX291 LVDS sensor file that the
# OpenIPC firmware ships, without distributing that file.
#
# divinus' [lvds] parser also requires sync_mode, data_endian, sync_code_endian and
# eight sync_code rows; the firmware file has none of the first three and four rows.
# The values added are the IMX290/IMX291 LVDS settings (SAV sync codes, big-endian
# data and sync codes). Rows 4-7 repeat row 3: divinus reads eight rows, uses four.
#
# Run on the camera:
#   sh make_sensor_ini.sh [source] [destination]

set -e

src=${1:-/etc/sensors/imx291_i2c_lvds_1080p_line.ini}
dst=${2:-/root/science/imx291_lvds_divinus.ini}

if [ ! -f "$src" ]; then
    echo "make_sensor_ini: $src not found" >&2
    exit 1
fi

if grep -q '^sync_code_endian' "$src"; then
    cp "$src" "$dst"
    echo "make_sensor_ini: $src already complete, copied to $dst"
    exit 0
fi

awk '
/^\[lvds\]/ {
    print
    print "sync_mode = 1                           ;LVDS_SYNC_MODE_SAV"
    print "data_endian = 1                         ;LVDS_ENDIAN_BIG"
    print "sync_code_endian = 1                    ;LVDS_ENDIAN_BIG"
    next
}
/^sync_code_3[ \t]*=/ {
    print
    row = substr($0, index($0, "="))
    for (i = 4; i < 8; i++)
        print "sync_code_" i " " row
    next
}
{ print }
' "$src" > "$dst.tmp"
mv "$dst.tmp" "$dst"
echo "make_sensor_ini: wrote $dst"
