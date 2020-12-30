# PPP over Serial (PPPoS) client library

Copy of the following repo with some additions (PIN submit, etc) for SIM7600x LTE modems. 

https://github.com/espressif/esp-idf/tree/master/examples/protocols/pppos_client

#### CMUX multiplexer protocol

This repository contains additions from [4688](https://github.com/espressif/esp-idf/issues/4688) which allows to use the data stream and AT commands in parallel over one UART connection.

#### Pin Assignment

The following pin assignments are used by default which can be changed in menuconfig.

| ESP32  | Cellular Modem |
| ------ | -------------- |
| GPIO25 | RX             |
| GPIO26 | TX             |
| GND    | GND            |
| 5V     | VCC            |

#### Usage

The library can be inserted into own projects via CMakeLists.txt file:

````
list(APPEND EXTRA_COMPONENT_DIRS "esp_lte_modem/components/modem")
set(IDF_EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS})
`````
