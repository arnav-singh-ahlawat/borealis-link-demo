# Firmware build

The two HEX and BIN images target ST's B-L072Z-LRWAN1 `b_l072z_lrwan1` with the STM32L072CZ. The BIN load address is `0x08000000`.

| Image | Flash used | RAM reserved |
|---|---:|---:|
| Transmitter | 63,752 bytes | 11,113 bytes |
| Receiver | 34,228 bytes | 13,976 bytes |

Build toolchain was Arm GNU 14.2.Rel1. The Zephyr version and module revisions are below. Licenses for the bundled firmware dependencies accompany this notice.

The transmitter was rebuilt on November 19, 2026 with photographs of Concordia's EV Building, Hall Building, and Loyola campus downloaded from the university's website. The three flash slots contain 24, 24, and 21 SSDV packets, respectively. The image directory includes the photo source links.

| Dependency | Source and revision |
|---|---|
| Zephyr | [v4.2.0](https://github.com/zephyrproject-rtos/zephyr/tree/v4.2.0) |
| CMSIS | [512cc7e](https://github.com/zephyrproject-rtos/cmsis/tree/512cc7e895e8491696b61f7ba8066b4a182569b8) |
| CMSIS 6 | [06d952b](https://github.com/zephyrproject-rtos/CMSIS_6/tree/06d952b6713a2ca41c9224a62075e4059402a151) |
| ST HAL | [1e75326](https://github.com/zephyrproject-rtos/hal_stm32/tree/1e753266ddfb4b07a8a0b1ec566e9637ea45d5ef) |
| LoRaMac-node | [fb00b38](https://github.com/zephyrproject-rtos/loramac-node/tree/fb00b383072518c918e2258b0916c996f2d4eebe) |
