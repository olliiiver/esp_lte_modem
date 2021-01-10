# PPP over Serial (PPPoS) client library

Copy of the following repo with some additions (PIN submit, etc) for SIM7600x LTE modems. 

https://github.com/espressif/esp-idf/tree/master/examples/protocols/pppos_client

#### CMUX multiplexer protocol

This repository contains additions from [4688](https://github.com/espressif/esp-idf/issues/4688) which allows the use of TCP/IP data streams and AT commands in parallel over one UART connection (two wire null modem).

#### Pin Assignment

The following pin assignments are used by default which can be changed in menuconfig.

| ESP32  | Cellular Modem |
| ------ | -------------- |
| GPIO25 | RX             |
| GPIO26 | TX             |
| GND    | GND            |
| 5V     | VCC            |

#### UART buffers

I had some problems with UART buffers (especially CONFIG_EXAMPLE_MODEM_UART_RX_BUFFER_SIZE) below 16KB.

#### Usage in other projects

The library can be inserted into your own projects. Just checkout this repo to the root of your project and insert the folloing into the main `CMakeLists.txt` file:

````
list(APPEND EXTRA_COMPONENT_DIRS "esp_lte_modem/components/modem")
set(IDF_EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS})
````

#### Monitor output 

Monitor output from example (pppos_client_main.c):

````
I (383) cpu_start: Pro cpu start user code
I (383) cpu_start: Application information:
I (383) cpu_start: Project name:     pppos_client
I (389) cpu_start: App version:      34b5e4d-dirty
I (394) cpu_start: Compile time:     Jan 10 2021 14:40:39
I (400) cpu_start: ELF file SHA256:  140ea96103be123a...
I (406) cpu_start: ESP-IDF:          v4.3-dev-1197-g8bc19ba89
I (413) heap_init: Initializing. RAM available for dynamic allocation:
I (420) heap_init: At 3FFAE6E0 len 00001920 (6 KiB): DRAM
I (426) heap_init: At 3FFB3B48 len 0002C4B8 (177 KiB): DRAM
I (432) heap_init: At 3FFE0440 len 00003AE0 (14 KiB): D/IRAM
I (438) heap_init: At 3FFE4350 len 0001BCB0 (111 KiB): D/IRAM
I (445) heap_init: At 4008B9E4 len 0001461C (81 KiB): IRAM
I (452) spi_flash: detected chip: generic
I (456) spi_flash: flash io: dio
I (461) cpu_start: Starting scheduler on PRO CPU.
I (0) cpu_start: Starting scheduler on APP CPU.
I (474) uart: queue free spaces: 30
I (484) pppos_example: Trying to initialize modem on GPIO TX: 17 / RX: 16
W (564) esp-modem: Rx Break
E (1984) esp-modem: esp_modem_dte_send_cmd(417): process command timeout
E (1984) dce_service: esp_modem_dce_sync(67): send command failed
E (1984) bg96: bg96_init(539): sync failed
I (2484) pppos_example: Trying to initialize modem on GPIO TX: 17 / RX: 16
E (3984) esp-modem: esp_modem_dte_send_cmd(417): process command timeout
E (3984) dce_service: esp_modem_dce_sync(67): send command failed
E (3984) bg96: bg96_init(539): sync failed
I (4484) pppos_example: Trying to initialize modem on GPIO TX: 17 / RX: 16
E (5984) esp-modem: esp_modem_dte_send_cmd(417): process command timeout
E (5984) dce_service: esp_modem_dce_sync(67): send command failed
E (5984) bg96: bg96_init(539): sync failed
I (6484) pppos_example: Trying to initialize modem on GPIO TX: 17 / RX: 16
E (7984) esp-modem: esp_modem_dte_send_cmd(417): process command timeout
E (7984) dce_service: esp_modem_dce_sync(67): send command failed
E (7984) bg96: bg96_init(539): sync failed
I (8484) pppos_example: Trying to initialize modem on GPIO TX: 17 / RX: 16
E (9984) esp-modem: esp_modem_dte_send_cmd(417): process command timeout
E (9984) dce_service: esp_modem_dce_sync(67): send command failed
E (9984) bg96: bg96_init(539): sync failed
I (10484) pppos_example: Trying to initialize modem on GPIO TX: 17 / RX: 16
E (11804) esp-modem: esp_dte_handle_line(138): handle line failed
W (11804) pppos_example: Unknow line received: RDY

E (11984) esp-modem: esp_modem_dte_send_cmd(417): process command timeout
E (11984) dce_service: esp_modem_dce_sync(67): send command failed
E (11984) bg96: bg96_init(539): sync failed
E (12264) esp-modem: esp_dte_handle_line(137): no handler for line
W (12264) pppos_example: Unknow line received: +CPIN: SIM PIN

I (12484) pppos_example: Trying to initialize modem on GPIO TX: 17 / RX: 16
I (12534) bg96: CMUX setup
I (12934) bg96: PIN ASK response: +CPIN: SIM PIN

I (12934) bg96: SIM needs PIN
I (13034) bg96: PIN ASK response: OK

I (13034) bg96: submit PIN
I (13144) bg96: set PIN ok
E (13464) esp-modem: esp_dte_handle_line(137): no handler for line
W (13464) pppos_example: Unknow line received: +CPIN: READY

E (13904) esp-modem: esp_dte_handle_line(137): no handler for line
W (13904) pppos_example: Unknow line received: SMS DONE

E (14594) esp-modem: esp_dte_handle_line(138): handle line failed
W (14594) pppos_example: Unknow line received: PB DONE

I (15344) bg96: CMUX command success
I (15344) bg96: enter CMUX mode ok
I (15894) pppos_example: Module: SIMCOM_SIM7600E
I (15894) pppos_example: Operator: "xxx"
I (15894) pppos_example: IMEI: xxx
I (15894) pppos_example: IMSI: xxx
I (16004) pppos_example: rssi: 0, ber: 0
I (16864) pppos_example: Battery voltage: 0 mV
I (16864) esp-modem: APN: xxx
I (16964) esp-modem: PPP MODE
I (16964) esp-modem: Got ATD
I (17014) esp-modem: Handle Line: CONNECT 115200
|� 115200
|�t",7
 for DLCI 1
I (17014) DCE_TAG: ATD response: CONNECT 115200
|� 115200
|�t",7

I (17064) pppos_example: Modem PPP Started
I (17314) esp-netif_lwip-ppp: Connected
I (17314) esp-netif_lwip-ppp: Name Server1: x.x.x.x
I (17314) esp-netif_lwip-ppp: Name Server2: x.x.x.x
I (17314) pppos_example: Modem Connect to PPP Server
I (17324) pppos_example: ~~~~~~~~~~~~~~
I (17324) pppos_example: IP          : x.x.x.x
I (17334) pppos_example: Netmask     : 255.255.255.255
I (17334) pppos_example: Gateway     : x.x.x.x
I (17344) pppos_example: Name Server1: x.x.x.x
I (17344) pppos_example: Name Server2: x.x.x.x
I (17354) pppos_example: ~~~~~~~~~~~~~~
I (17354) pppos_example: GOT ip event!!!
I (17364) system_api: Base MAC address is not set
I (17364) system_api: read default base MAC address from EFUSE
I (17374) pppos_example: MQTT other event id: 7
I (17764) pppos_example: MQTT_EVENT_CONNECTED
I (17764) pppos_example: sent subscribe successful, msg_id=20524
I (17864) pppos_example: MQTT_EVENT_SUBSCRIBED, msg_id=20524
I (17864) pppos_example: sent publish successful, msg_id=0
I (17964) pppos_example: MQTT_EVENT_DATA
TOPIC=/topic/esp-pppos
DATA=esp32-pppos
I (18164) pppos_example: rssi: 0, ber: 0
I (23264) pppos_example: rssi: 0, ber: 0
I (28364) pppos_example: rssi: 0, ber: 0
I (33464) pppos_example: rssi: 0, ber: 0
````