# Zephyr driver: Hi-Link LD2450 multi-target radar

A Zephyr RTOS sensor driver for the Hi-Link LD2450 24 GHz mmWave radar. The
sensor tracks up to three targets and reports each one's position (X, Y),
radial speed, and distance resolution over UART.

This repository is laid out exactly like the Zephyr source tree, so the files
can be copied into a Zephyr fork for an upstream pull request. Until then it
builds as an out-of-tree Zephyr module.

```text
drivers/sensor/hilink/ld2450/ld2450.c           Driver (interrupt-driven UART)
drivers/sensor/hilink/ld2450/ld2450_protocol.h  Frame decoder (no Zephyr deps)
include/zephyr/drivers/sensor/ld2450.h          Public channels
dts/bindings/sensor/hilink,ld2450.yaml          Devicetree binding
samples/sensor/ld2450/                          Demo application
tests/drivers/sensor/ld2450/                     Zephyr tests using an emulated UART
tests/host/                                     Host unit tests for the decoder
.github/workflows/build.yml                     Cloud build and tests
west.yml                                        Zephyr workspace manifest
```

## Sensor API

| Channel | Value |
|---|---|
| `SENSOR_CHAN_PROX` | 1 if any target is detected, otherwise 0 |
| `SENSOR_CHAN_DISTANCE` | Distance to the nearest target, in meters |
| `SENSOR_CHAN_LD2450_TARGET_COUNT` | Number of detected targets (0-3) |
| `SENSOR_CHAN_LD2450_TARGET_X(n)` / `_Y(n)` | Target position, in meters |
| `SENSOR_CHAN_LD2450_TARGET_SPEED(n)` | Radial speed in m/s; positive is moving away |
| `SENSOR_CHAN_LD2450_TARGET_RESOLUTION(n)` | Distance resolution, in meters |

Per-target channels return `-ENODATA` when that target slot is empty.
`sensor_sample_fetch()` waits for the next complete frame, so values are never
stale.

## Wiring (ESP32-S3 DevKitC)

| LD2450 pin | ESP32-S3 DevKitC pin |
|---|---|
| 5V | 5V |
| GND | GND |
| TX | GPIO18 (UART1 RX) |
| RX | GPIO17 (UART1 TX) |

The sensor's factory baud rate is 256000.

## Run the host unit tests

Only a C compiler is needed:

```sh
cd tests/host
gcc -std=c99 -Wall -Wextra -Werror -I../../drivers/sensor/hilink/ld2450 test_ld2450_protocol.c -o test_ld2450_protocol
./test_ld2450_protocol
```

## Build in the cloud (no installation)

Every push to GitHub runs `.github/workflows/build.yml`, which:

1. Runs the host unit tests.
2. Runs the Zephyr driver tests on `native_sim`, feeding simulated radar
   frames through an emulated UART.
3. Builds the demo for the ESP32-S3 DevKitC.
4. Uploads the firmware as an artifact named `ld2450-esp32s3-firmware`.

To get the firmware, open the repository's **Actions** tab, select the latest
successful run, and download the artifact. It contains `ld2450-esp32s3.bin`
and `flash-address.txt`.

## Flash from the browser (no installation)

1. Open the [Espressif ESP Launchpad](https://espressif.github.io/esp-launchpad/)
   or [esptool-js](https://espressif.github.io/esptool-js/) in Chrome or Edge.
2. Connect the board's **UART** USB port and click **Connect**. If it doesn't
   connect, hold **BOOT**, press **RESET**, then release **BOOT**.
3. Choose `ld2450-esp32s3.bin`, set the address from `flash-address.txt`,
   and click **Program**.
4. Press **RESET**, open the console at **115200** baud, and walk in front of
   the sensor.

Expected output with one person in front of the sensor:

```text
targets: 1
  target 1: x=-0.782 m  y= 1.713 m  range= 1.883 m  azimuth= -24.5 deg  speed= -0.16 m/s
```

The sign of X, and therefore of the azimuth, follows the sensor's own axis.
Confirm which side is positive by walking left and right in front of it.

## Build locally (optional)

Follow the
[Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
to install the host tools, then use this repository as the west manifest:

```sh
west init -m https://github.com/Pooja-MAIN/zephyr-ld2450 ld2450-workspace
cd ld2450-workspace
west update
west zephyr-export
west packages pip --install
west sdk install -t xtensa-espressif_esp32s3_zephyr-elf
west blobs fetch hal_espressif
west build -p -b esp32s3_devkitc/esp32s3/procpu zephyr-ld2450/samples/sensor/ld2450
west flash
```
## Upstream submission checklist

1. Check your employer's policy on open-source contributions.
2. Fork `zephyrproject-rtos/zephyr` and copy the files into the same paths.
3. Upstream-only edits:
   - Add `hilink	Shenzhen Hi-Link Electronic Co., Ltd.` to
     `dts/bindings/vendor-prefixes.txt` in alphabetical order. Delete this
     repository's `dts/bindings/vendor-prefixes.txt`.
   - Add `rsource "hilink/Kconfig"` to `drivers/sensor/Kconfig` and
     `add_subdirectory(hilink)` to `drivers/sensor/CMakeLists.txt`, both in
     alphabetical order.
   - Delete the root `CMakeLists.txt`, `Kconfig`, `west.yml`,
     `zephyr/module.yml`, `.github/`, and `tests/host/`.
   - Add a `README.rst` for the sample, following other sensor samples.
   - Add the device to `tests/drivers/build_all/sensor/uart.dtsi`.
4. Split the work into commits: vendor prefix, driver and binding, sample.
5. Sign off every commit (`git commit -s`) and run `./scripts/ci/check_compliance.py`.
6. Test on hardware, then open the pull request with a short demo video.
