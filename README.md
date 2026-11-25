# BOREALIS optical-link demo

Two Zephyr builds for the **B-L072Z-LRWAN1 Discovery kit** and a Dear PyGui receiver console. The transmitter sends one of three flash-resident images. The laptop shows the received LoRa payload and Manchester chips, the Manchester-decoded bytes, the Reed–Solomon result, and the reconstructed JPEG.

The demo uses the capstone's 256-byte SSDV packets, their CRC and RS(255,223) parity, and Manchester-coded OOK chip values. LoRa carries the packed chip values and supplies datagram boundaries. Laser drive, optical clock recovery, detector behavior, and optical BER need the physical optical link.

## Run the demo

1. Label the two **B-L072Z-LRWAN1** boards TX and RX. Attach the appropriate antennas. Flash [borealis-tx.hex](release/borealis-tx.hex) onto TX and [borealis-rx.hex](release/borealis-rx.hex) onto RX with STM32CubeProgrammer over ST-LINK/SWD. Connect one board at a time while flashing. The equivalent BIN files start at address **0x08000000**.
2. Connect RX to the laptop through its **ST-LINK USB** port. The receiver publishes packet records through the ST-LINK virtual COM port at **115200, 8N1**. TX just needs power once programmed. [Discovery kit connections](https://docs.zephyrproject.org/4.2.0/boards/st/b_l072z_lrwan1/doc/index.html)
3. From the unzipped project folder in a Python 3.11 or 3.12 environment, install the desktop dependencies and start the viewer.

   ```sh
   python -m pip install -r desktop/requirements.txt
   python desktop/viewer.py
   ```

4. Pick RX's COM port and click **Connect receiver**. Wait for **Receiver listening**. Then tap TX's user button. Keep successive taps within **450 ms**.

| TX button | Flash image | SSDV packets |
|---|---|---:|
| 1 press | Concordia EV Building | 24 |
| 2 presses | Concordia Hall Building | 24 |
| 3 presses | Concordia Loyola Campus | 21 |

The three images are photographs downloaded from Concordia University's website, showing its EV Building, Henry F. Hall Building, and Loyola campus in Montréal, Québec. Each photo is stored as a 192 × 128 JPEG and a pre-encoded SSDV stream. The [photo sources](images/README.md) identify the files and link to the original web images and pages.

The blue TX LED stays on during the transfer. Wait for it to go out before selecting another image. A third press starts transmission immediately. A single or double press starts after the press window closes.

Click a row in **Received packets** to inspect its chips and paired decoded frame. The **After RS and CRC** tab shows the exact bytes passed to reconstruction. The image appears when all CRC-validated frames arrive. **Preview partial image** rebuilds from the frames available so far. To repeat an image, make the same gesture after the TX LED goes out.

### Rehearse without devices

Click **Replay demo**, or run `python desktop/viewer.py --replay`. The shipped capture exercises the EV Building, Hall Building, and Loyola Campus photos in that order, using 207 LoRa datagrams. In image 2 (Hall Building), select SSDV frame 1, fragment 1 to see a deliberately invalid Manchester pair and a repaired RS symbol. Replay signal levels are synthetic. On the physical link, radio-layer CRC can drop a datagram before the application receives it, so a missing fragment and a corrected SSDV byte are different outcomes.

### If packets are missing

- If the receiver has no serial port, check the ST-LINK USB connector, data cable, and the serial port listed by your OS. Close terminal programs using that port.
- If **Receiver listening** appears but the packet table is empty, check the TX LED, the two firmware roles, antennas, and that both firmware builds use the same frequency. TX writes initialization and send errors to its own ST-LINK console.
- The default radio profile is point-to-point LoRa at **915 MHz**, **500 kHz**, **SF7**, **4/5**, **2 dBm** TX power, with a **45 ms** inter-datagram gap. Set `CONFIG_BOREALIS_FREQUENCY_HZ` in both apps' `prj.conf` and rebuild both if your bench uses a different frequency.

## Rebuild the firmware

Use a Zephyr 4.2.0 workspace with the Arm SDK and host build tools. Run these commands from a Python virtual environment with `west` installed.

```sh
west init -m https://github.com/zephyrproject-rtos/zephyr --mr v4.2.0 zephyr-workspace
cd zephyr-workspace
west update cmsis cmsis_6 hal_stm32 loramac-node
west zephyr-export
west packages pip --install
west sdk install --toolchains arm-zephyr-eabi
```

The build commands assume the unzipped `borealis-link-demo` folder is next to `zephyr-workspace`.

```sh
west build -p always -b b_l072z_lrwan1 -d build/tx ../borealis-link-demo/firmware/transmitter
west build -p always -b b_l072z_lrwan1 -d build/rx ../borealis-link-demo/firmware/receiver
```

Each build produces its own `zephyr/zephyr.hex`. To use a configured west flash runner, connect one board and run `west flash -d build/tx` or `west flash -d build/rx` as appropriate. [Zephyr installation guide](https://docs.zephyrproject.org/4.2.0/develop/getting_started/index.html)

To replace the built-in images, run the following from the demo folder after installing the desktop dependencies. The script normalizes each input to a 192 × 128 baseline JPEG, encodes SSDV offline, verifies the reconstruction, and writes the three slots. Rebuild and reflash TX.

```sh
python tools/prepare_images.py image1.jpg image2.jpg image3.jpg
```

## Protocol and STM32H7 migration

| Layer | Files | Responsibility |
|---|---|---|
| Portable C protocol | `protocol/` | Packed Manchester coding, LoRa fragment framing, SSDV RS/CRC checks, serial record format |
| Zephyr application | `firmware/transmitter/main.c` and `firmware/receiver/main.c` | Press grouping, image selection, radio queue, packet decoding and reporting |
| Platform adapters | `radio_zephyr.c`, `controls_zephyr.c`, `serial_zephyr.c`, and each app's `boards/` | Zephyr LoRa, GPIO keys, LEDs, UART, and devicetree bindings |
| Laptop | `desktop/` | USB serial intake, packet inspector, session reassembly, SSDV-to-JPEG display |

On TX, Zephyr's GPIO-keys driver debounces the button. A delayable work item classifies the presses and wakes the transmit task through a message queue. On RX, Zephyr's asynchronous LoRa callback copies the datagram to a bounded queue. A worker validates, decodes, and writes serial records. The Zephyr board definition and SX1276 driver handle radio pins and antenna-switch sequencing.

For an STM32H7 board running the same radio demo, supply a devicetree overlay mapping `lora0` to the external radio, `sw0` to a GPIO key, `demo-led` to the status LED, and `borealis,telemetry-uart` to the UART exposed to the laptop. Keep the GPIO-keys input mode synchronous for the TX work-queue callback. The protocol, press behavior, and desktop program are board-independent.

For the optical port, feed `demo_manchester_encode()` into a timer/DMA-driven chip output. On RX, present chip-aligned, frame-aligned data from the comparator capture and clock-recovery layer to `demo_manchester_decode()`, then call `demo_ssdv_repair()`. The SSDV frames and CRC rules carry over. The LoRa fragment header and radio diagnostics belong to the demo transport.

### LoRa demo header

All multibyte fields are big-endian. The header precedes packed Manchester chips. The convention is MSB first, `0 → 01` and `1 → 10`. Chip value 1 represents optical ON.

| Offset | Bytes | Field |
|---|---:|---|
| 0 | 2 | Magic `BL` |
| 2 | 1 | Wire version `1` |
| 3 | 1 | Image slot `1..3` |
| 4 | 4 | Transfer ID |
| 8 | 2 | SSDV frame index, zero-based |
| 10 | 2 | SSDV frames in this transfer |
| 12 | 1 | Fragment index `0..2` |
| 13 | 192 / 192 / 128 | Packed chips for 96 / 96 / 64 SSDV bytes |

The receiver sends newline-delimited JSON records of kind `radio`, `frame`, and `status`. A radio record includes the exact LoRa payload, RSSI, SNR, and arrival time. A frame record preserves the pre-FEC bytes and includes the repaired packet for valid CRC and metadata. Loss of a whole LoRa fragment leaves an incomplete SSDV frame. Complete frames can still be used for partial image preview.

## Verification

The bundled TX and RX binaries were cross-built for `b_l072z_lrwan1` with Zephyr 4.2.0 and Arm GNU 14.2.Rel1. The transmitter was rebuilt with the Concordia photo set. The portable test reassembles all 69 SSDV frames byte-for-byte through production Manchester and radio framing code, verifies correction through 16 damaged RS symbols, checks duplicate and missing-frame behavior, and rebuilds all three JPEGs. The desktop dependencies are the same for replay and serial mode.

```sh
python tests/check.py
```

With a host C compiler and the desktop dependencies installed, add `--write-replay` to regenerate the offline capture. Physical flash, button, RF, and USB bench operation still need the two boards.

## Third-party components

- [Phil Heron's SSDV](https://codeberg.org/fsphil/ssdv), revision `d1ceda81b69f88741396f3e052b50c8ae40efb76`, runs as a separate WASI command on the laptop. The original source and license are under `third_party/ssdv/`. To rebuild it with Zig 0.15.2 from the demo folder, run `zig cc -target wasm32-wasi -Oz -Wl,--strip-all third_party/ssdv/main.c third_party/ssdv/ssdv.c third_party/ssdv/rs8.c -o desktop/ssdv.wasm`.
- The compiled RTOS and board support dependencies and their license notices are in `release/NOTICE.md`. The desktop uses Inter and IBM Plex Mono, with their Open Font License texts under `third_party/`.
