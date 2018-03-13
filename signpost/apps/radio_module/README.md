Radio Module
============

The Signpost Radio Module application takes data sent from other Signpost
modules and sends it over LoRa and Cellular as a best-effort service.

The radio_app_ble also can send this data over BLE given an appropriate receiver
such as the one found in tool/static_ble.

The radio_app_muling stores networking data on the SD card and sends it
when there is a data mule (like a drone or phone) nearby
