# Science Mode (Hi3516CV300)

Science mode is a build option of Divinus for meteor cameras. It turns off the image processing that alters faint or moving point sources, encodes every frame as an independent H.264 picture, and attaches to each frame a record of when it was captured and with which exposure, gains and white balance. A text control server on TCP port 9600 sets exposure, white balance and the clock at run time.

The per-frame record is the RMSP v5 SEI read by the `alpha2` branch of the [RMS](https://github.com/CroatianMeteorNetwork/RMS) meteor station software (`RMS/RMSPSei.py`), and the control server speaks the text protocol RMS `alpha2` uses for OpenIPC cameras (`Utils/CameraControl.py`, `ispControl()`). The implementation here is independent; no RMS code is included.

## What it changes and why

| Change | Where | Reason |
|--------|-------|--------|
| ISP noise reduction, sharpening, DRC, dehaze, anti-false-colour, crosstalk removal, chromatic aberration (CA, CAC) and lens shading bypassed; defect pixel correction bypassed unless `dpc = 1` | ISP module control, reasserted every 10 s | Temporal and spatial filters dim fast meteors and change star profiles; with them off, an output value depends only on the light, exposure, gains, white balance, colour matrix and gamma |
| VPSS noise reduction, image enhancement, DCI and sharpening off | VPSS group attributes | Same reason, second processing stage |
| All-intra coding | `gop: 1` in `divinus.yaml` | Each frame is coded on its own: no motion-compensated prediction across frames, and a lost frame does not damage the next ones. Costs bitrate |
| Deblocking filter off | every H.264 channel at creation | The deblocking filter smooths block edges and with them single-pixel stars |
| VBR quantiser range 12 to 48 | H.264 VBR rate control | Keeps faint detail at QP 12 when the bitrate allows; `minQual`/`maxQual` from the configuration are not used in this mode |
| Gamma 0.5 starting at the sensor black level | ISP gamma table, reasserted every 10 s | A square-root curve spends 8-bit codes evenly relative to photon noise and is easy to invert. The CV300 does not remove the sensor black level (240 of 4095 on the IMX291) before the gamma table: `HI_MPI_ISP_SetBlackLevelAttr` from 0 to 480 left the output unchanged. A curve starting at 0 put black at 62 of 255, where the curve is flat, so the night sky and stars of magnitude 4 to 5 shared 1 or 2 output codes. The table starts at `pedestal` instead (below) |
| Fixed exposure and gains at night, unity white balance, configurable saturation | night baseline from `science.conf` | Constant response through the night for photometry |
| Automatic exposure at a fixed frame rate and automatic white balance by day | day mode | Daylight images for calibration and checks |
| Day/night switch by sun altitude | maintenance thread | No light sensor or IR-cut logic is involved; the station position and a sun-altitude threshold decide |
| ISP frame rate trim | `sensor_fps` | On the IMX291 an ISP rate of 25.00 makes each frame one sensor line too long (24.98 fps); 25.01 gives 40.000 ms frames |
| Per-frame RMSP v5 SEI | before the first slice of each H.264 access unit | Capture time, exposure, gains, white balance, line period, QP and SoC temperature for every frame |
| VPSS output at the capture size, 8 video buffer blocks | v3 HAL | The CV300 VPSS never starts a job when its output size differs from its input; the encoder scales instead |

The gamma table has 257 nodes, 16 input codes apart (12-bit in, 12-bit out). With pedestal P, node i holds `round(4095 * sqrt(max(0, 16 i - P) / (4096 - P)))`, and the last node is 4095.

Setting the environment variable `SCI_VPSS_NR` before starting Divinus leaves VPSS noise reduction on, for comparisons.

## Supported hardware

Hi3516CV300 with a Sony IMX291 sensor over LVDS (board type 50H20L). Tested on one camera, in use with RMS `alpha2` since 7 November 2026. The code is part of the `hisi-gen3` HAL, which also drives the Hi3516EV100; that chip and other sensors have not been tested. Another sensor needs its own sensor file and its own `pedestal` value.

## Building

Use the OpenIPC musl toolchain for the CV300 (we built with `arm-openipc-linux-musleabi-gcc` 13.3 from OpenIPC's Hi3516CV300 buildroot SDK):

```
make -C src CC=/path/to/arm-openipc-linux-musleabi-gcc OPT="-Os -s -DSDK_OFF32 -DSCIENCE"
```

The result is `./divinus` (about 600 KB). `-DSCIENCE` enables science mode. `-DSDK_OFF32` was needed on the camera tested, with or without science mode: its vendor libraries were built against uClibc without large-file support and call `mmap()` with a 32-bit offset, which Divinus then treats as unsigned (MMZ addresses are above 2 GiB). Without it the ISP fails to start (`0xa01c8045`, statistics buffers cannot be mapped).

`src/hal/hisi/v3_science.h` declares the vendor structures science mode uses. `tools/science/abi_check.sh` compares their sizes, alignments, member offsets and module-control bits with the vendor headers at compile time; the vendor headers are not part of this repository (OpenIPC's `openhisilicon` repository has them under `kernel/include/hi3516cv300`):

```
CC=/path/to/arm-openipc-linux-musleabi-gcc tools/science/abi_check.sh /path/to/openhisilicon/kernel/include/hi3516cv300
```

## Installing on an OpenIPC camera

This project provides no flash images, no vendor libraries and no Majestic. Divinus uses the MPI libraries and the sensor driver of the OpenIPC firmware already on the camera.

1. Install the official OpenIPC firmware for your camera yourself, following OpenIPC's instructions, and check that the stock streamer (Majestic) shows a picture.
2. Create `/root/science` on the camera and copy into it the `divinus` binary, `res/science/divinus.yaml`, `res/science/science.conf` and `res/science/make_sensor_ini.sh`. Copy `res/science/S96science` to `/etc/init.d/S96science` and make it executable.
3. Edit `/root/science/science.conf`: at least `lat` and `lon` (see below).
4. Create the sensor file on the camera from the one the firmware ships: `sh /root/science/make_sensor_ini.sh`. It reads `/etc/sensors/imx291_i2c_lvds_1080p_line.ini` and writes `/root/science/imx291_lvds_divinus.ini`, adding the LVDS keys the Divinus parser requires (`sync_mode`, `data_endian`, `sync_code_endian`, `sync_code_4` to `sync_code_7`). The firmware's file is not redistributed here.
5. Stop Majestic from starting: `chmod a-x /etc/init.d/S95majestic`.
6. Set up time keeping. The CV300 has no real-time clock. Use the firmware's NTP client (`/etc/ntp.conf`, or the `time ntp host` command below), chrony, or `time set` over port 9600. SEI capture times are only as good as this clock.
7. Reboot. `S96science` starts Divinus from `/root/science`, restarts it 5 s after any exit and logs to `/tmp/divinus.log` (in RAM; cut to its last 50 KB at a restart once over 200 KB). Lines from science mode start with `[science]`.
8. Check: `echo query | nc <camera> 9600` prints the state, and the stream is at `rtsp://<camera>:554/`.

### divinus.yaml

The provided file streams H.264 at 1280x720 and 25 fps over RTSP on port 554, all-intra (`gop: 1`), VBR at 10 Mbit/s, High profile (`profile: 2`), with the sensor at 1920x1080 and the encoder scaling down. Set `width: 1920` and `height: 1080` for native resolution. OSD, ONVIF, mDNS, audio, JPEG and night mode GPIO handling are off. The web server listens on port 8081. See [Configuration](config.md) for the other keys.

### science.conf

`/root/science/science.conf` is read once at start. Each line is `key = number`; lines starting with `#` and lines whose value is not a number are ignored. Gains are x1024 (1024 = 1x), exposure times in microseconds.

| Key | Default | Meaning |
|-----|---------|---------|
| `lat`, `lon` | unset | Station position, decimal degrees, north and east positive. While either is unset there is no day/night switching and the camera stays in the night baseline |
| `switch_deg` | -5 | Night below this sun altitude [deg] |
| `night_again` | 22924 | Night sensor analogue gain |
| `night_dgain` | 1024 | Night sensor digital gain |
| `night_ispdgain` | 2048 | Night ISP digital gain |
| `night_exp` | 39941 | Night exposure [us] |
| `night_satu` | 0 | Night saturation, 0 to 255 (0 = black and white) |
| `mono_from_utc`, `mono_to_utc` | unset | UTC hours (fractions allowed, may wrap past midnight) during which night saturation is 0 |
| `day_max_exp` | 39970 | Longest automatic exposure by day [us]; the shortest is 30 us |
| `day_max_dgain` | 1024 | Highest automatic sensor digital gain by day |
| `day_satu` | 128 | Day saturation |
| `sensor_fps` | unset | ISP frame rate, applied 2 s after start (1 to 60) |
| `chroma_qp_offset` | 0 | H.264 chroma QP offset, -12 to 12; positive values quantise colour more coarsely than luma |
| `dpc` | 0 | 1 = defect pixel correction active, 0 = bypassed |
| `pedestal` | 240 | 12-bit sensor black level the gamma table starts at, 0 to 2048 |
| `black_level` | unset | 12-bit black level written to the ISP black level attribute and reasserted; unset leaves the sensor default. Had no visible effect on the CV300 tested |

The night defaults are IMX307 values used as a starting point; tune them for each camera and site. The night baseline sets manual exposure and gains, unity white balance (256/256/256), the colour matrix on and the night saturation. Day mode sets automatic exposure at a fixed frame rate, automatic white balance, the colour matrix on and the day saturation. A switch happens only when the sun crosses `switch_deg`, so settings sent over port 9600 hold until the next crossing.

## Control port 9600

One command per TCP connection: send a line ending in a newline (at most 511 bytes; the server waits up to 3 s), read the reply, and the server closes the connection. Commands are handled one at a time. There is no authentication: keep port 9600 (and the RTSP and web ports) on a trusted network.

`OK` or `FAIL` in a reply reflects the return code of the vendor call. Settings sent here are not saved and last until Divinus restarts; the maintenance thread reasserts the ISP module bypasses, black level, gamma and VPSS settings every 10 s.

| Command | Action | Reply |
|---------|--------|-------|
| `query` (or an empty line) | Report the state | Lines `Mode:` (day, night or unknown), `AE:` (manual or auto), `Exposure: <us> us`, `AGain:`, `DGain:`, `ISPDGain:`, `AE Route: fixed frame rate, lines/500ms <n>, fps <fps x100>`, `WB: R <r> Gr <gr> Gb <gb> B <b> (x256), colortemp <K> K`, `SocTemp: <C> C`, `ModuleControl: <hex> (NR <0/1> sharpen <0/1> DPC <0/1> DRC <0/1> bypassed)`, `BlackLevel: <4 values>`, `Science: gamma 0.5 above pedestal <P>, deblocking off, all-intra, RMSP v5 SEI` |
| `manual [-a <again>] [-d <dgain>] [-i <ispdgain>] [-e <us>]` | Manual exposure; omitted values keep the current ones (the night baseline if they cannot be read) | `manual a=<> d=<> i=<> e=<>: OK` |
| `auto [--min-exptime <us>] [--max-exptime <us>] [--max-dgain <x1024>]` | Automatic exposure at a fixed frame rate; omitted limits are left as they are | `auto: OK` |
| `wb <r> <g> <b>` | Manual white balance gains (x256); `g` applies to Gr and Gb | `wb: OK` |
| `wb auto` | Automatic white balance | `wb: OK` |
| `wb` | Unity white balance (256/256/256) | `wb: OK` |
| `satu <0-255>` | Manual saturation | `satu <n>: OK` |
| `fps <rate>` | ISP frame rate, between 1 and 60 exclusive | `fps <rate>: OK` |
| `ccm on` / `ccm off` | Colour matrix on or bypassed (any word other than `on` means off) | `ccm <word>: OK` |
| `dpc on` / `dpc off` | Defect pixel correction on or bypassed, kept by the maintenance thread | `dpc on: OK` |
| `blc set <n>` | Write and keep a 12-bit ISP black level | `blc <4 values> (conf <n>): OK` |
| `blc get` | Read the ISP black level | as above |
| `pedestal <n>` | Rebuild and apply the gamma table from black level `n` (clamped to 0-2048) | `pedestal <P>: OK` |
| `modctrl get` | Read the ISP module control word | `modctrl <hex> (enforce on): OK` |
| `modctrl set <hex>` | Write the module control word as given | as above |
| `enforce on` / `enforce off` | Whether the maintenance thread reasserts the module bypasses (off for experiments with `modctrl set`) | `enforce on: OK` |
| `ircut day` / `ircut night` | Sets the internal day/night flag only, for the night saturation window; drives no IR-cut filter and changes no exposure | `ircut <word>: OK (mode only, no IR-cut actuator driven)` |
| `time` | Read the camera clock | `time: YYYY-MM-DD HH:MM:SS UTC` |
| `time set <unix seconds>` | Set the camera clock | `time set: OK` |
| `time ntp host <host>` | Write `server <host> iburst` to `/etc/ntp.conf` and restart `/etc/init.d/S49ntpd` | `time ntp host <host>: OK` |
| `reboot` | Sync and reboot after 1 s | `reboot: OK` |
| any other word (`encode`, `osd`, `flip`, `mirror`, `colortone`, ...) | Nothing; these are fixed in `divinus.yaml` | `<word>: ignored (fixed in divinus.yaml / not applicable)` |

`blc` and `modctrl` read when the second word is anything other than `set`. Example: `printf 'manual -a 16384 -e 39941\n' | nc 192.168.1.10 9600`.

## RMSP v5 SEI

Each H.264 access unit carries one SEI NAL unit, placed before its first slice NAL unit (after SPS and PPS on key frames). It is written by `science_sei()` in `src/hal/hisi/v3_science.c` and describes the frame it travels with.

**NAL unit.** Start code `00 00 00 01`, NAL header `06` (SEI, nal_ref_idc 0), payload type `05` (user data unregistered), payload size `4D` (77 bytes = 16 + 61), the 16-byte UUID, the 61-byte record, and the RBSP trailing byte `80`. Emulation prevention bytes (`03`) are inserted wherever two zero bytes would be followed by a byte of 3 or less; readers remove them first.

**UUID.** The ASCII bytes of `science-frame-v5`: `73 63 69 65 6e 63 65 2d 66 72 61 6d 65 2d 76 35`.

**Record.** 61 bytes, integers little-endian. Offsets below are within the record, before the XOR step.

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | Magic `RMSP` (ASCII) |
| 4 | 1 | Version, 5 |
| 5 | 1 | Flags (below) |
| 6 | 4 | Camera wall clock, seconds (`gettimeofday`) |
| 10 | 4 | Camera wall clock, microseconds |
| 14 | 4 | Frame sequence number from the encoder |
| 18 | 4 | Exposure time [us] |
| 22 | 4 | Sensor analogue gain, x1024 |
| 26 | 4 | Sensor digital gain, x1024 |
| 30 | 4 | ISP digital gain, x1024 |
| 34 | 2 | White balance R gain, x256 |
| 36 | 2 | White balance B gain, x256 |
| 38 | 2 | White balance G gain, x256 (mean of Gr and Gb) |
| 40 | 2 | Colour temperature estimate [K] |
| 42 | 2 | SoC temperature x10 [deg C], signed |
| 44 | 4 | Frame PTS [us], low 32 bits (MPP clock, from the encoder) |
| 48 | 4 | MPP clock [us], low 32 bits, read right after the wall clock |
| 52 | 4 | Sensor line period [ns] |
| 56 | 2 | Lines per frame (VMAX) |
| 58 | 1 | Mean QP of the frame |
| 59 | 1 | Rate control start QP of the frame, 0 if unknown |
| 60 | 1 | Checksum |

**Flags.** Bit 0: temperature valid. Bit 1: white balance fields valid. Bit 3: exposure and gain fields valid. Bit 5: line period and VMAX valid. Bit 6: QP fields valid. Bit 7 (gains looked up for this exact frame) is defined by the RMS reader and never set by this writer. Fields whose flag is clear are zero.

**Checksum and XOR.** The checksum is the XOR of record bytes 4 to 59. Then bytes 4 to 60 (version, flags, fields and checksum) are XORed with `FF`; the magic stays readable. A reader finds `RMSP`, removes emulation prevention, XORs bytes 4 to 60 with `FF`, checks version 5 and the checksum.

**Timing.** Wall clock and MPP clock are read back to back when the encoder delivers the frame, so the capture time on the camera clock is

```
capture_utc = (sec + usec / 1e6) - ((mpp_now - frame_pts) mod 2^32) / 1e6
```

Both counters wrap every 2^32 us (71.6 min); the difference stays valid across a wrap. RMS `alpha2` median-filters `utc - mpp` over neighbouring frames and subtracts the exposure time and a fixed readout offset to get the start of integration.

**Sources.** Exposure, gains and line timing come from `HI_MPI_ISP_QueryExposureInfo`, white balance from `HI_MPI_ISP_QueryWBInfo`, both read when the frame is encoded, so they can lag an exposure change by a frame or two. When a port 9600 command or the maintenance thread holds the ISP lock, those fields are left out of that frame's record (flags clear). Line period = 500 000 000 / lines per 500 ms; VMAX = lines per 500 ms x 2 / frame rate. SoC temperature is read at most once per second.

## Rollback

On the camera:

```
/etc/init.d/S96science stop
chmod a-x /etc/init.d/S96science
chmod a+x /etc/init.d/S95majestic
reboot
```

Science mode changes nothing outside `/root/science` and `/etc/init.d` (and `/etc/ntp.conf` if `time ntp host` was used). Removing `/root/science` and `/etc/init.d/S96science` leaves the firmware as installed. OpenIPC's `firstboot` command resets all user changes on the camera, including network settings.

## Licence

Science mode is part of Divinus and under the same MIT licence (see `LICENSE`). The repository contains no vendor SDK files: the structures in `src/hal/hisi/v3_science.h` are declared in this project's own terms, as for the rest of the HAL, and checked against the vendor headers with `tools/science/abi_check.sh`.
