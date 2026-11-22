# Meteor Science Firmware

A science mode for meteor cameras built on a Hi3516CV300 SoC with a Sony IMX291 sensor (the 50H20L module used by many meteor stations). It runs on top of the official OpenIPC firmware and replaces its streamer with one that keeps the image fit for photometry and timing:

- no noise reduction, sharpening, dynamic range compression or other image processing that alters faint or moving point sources
- every frame encoded as an independent H.264 picture (all-intra), deblocking off
- a gamma 0.5 curve starting at the sensor black level, so faint signal keeps its levels
- a per-frame RMSP v5 SEI record with capture time, exposure, gains, white balance and encoder QP, as read by the `alpha2` branch of [RMS](https://github.com/CroatianMeteorNetwork/RMS)
- a text control server on TCP 9600 (exposure, gains, white balance, clock), limited to listed clients
- day and night switching by sun altitude on the camera itself

**Status: experimental, for testing.** See the status section of the documentation for what has been measured and what is open.

## Documentation

[doc/science.md](doc/science.md): what it changes and why, supported hardware, building, installing on an OpenIPC camera, time keeping with chrony, RMS settings, the control protocol, the SEI record layout, and rollback.

## Download

Prebuilt binaries are on the Releases page. To build from source, see [doc/science.md](doc/science.md#building).

No flash images, vendor libraries or vendor SDK files are distributed. The camera keeps its OpenIPC firmware and vendor libraries; this project adds a streamer, its configuration and an init script.

## Licence

MIT (see `LICENSE`). The streamer is built on Divinus (OpenIPC, MIT); its original documentation is in [doc/divinus.md](doc/divinus.md). The libraries in `src/lib` keep their own licences.
