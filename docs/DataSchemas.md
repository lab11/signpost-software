Current Signpost Data Schemas
=====================

These are the data and associated data formats that are currently being
collected by deployed Signposts. This data is being sent to the internal
Lab11 Influx database, the [Global Data Plane (GDP)](https://swarmlab.eecs.berkeley.edu/projects/4814/global-data-plane),
and can be subscribed to using MQTT. Please contact us to set up an
MQTT username and password.

### Stream/Log Naming

MQTT: signpost/lab11/<device_short_name> 

(i.e. signpost/lab11/gps)


GDP: edu.berkeley.eecs.<signpost_mac_lower>.<signpost_device_long>.<version> 

(i.e. edu.berkeley.eecs.c098e5120003.signpost_energy.v0-0-1)

### Deployed Signposts

All signpost will report energy, gps, and radio status data.
Signpost will also report data based on their currently installed modules.
Installed and working modules are listed below next to the signpost mac address.

Currently the following signposts are deployed:
  - c098e5120001 (microwave radar, audio, rf spectrum, ambient)
  - c098e5120003 (audio)
  - c098e5120004 (audio, rf spectrum, ambient)

Schemas
-------


### Common

All data packets include a `_meta` section like the following:

```
{
    "_meta": {
        "received_time":    <time in ISO-8601 format>,
        "device_id":        <signpost MAC>,
        "receiver":         <lora_or_http>,
        "gateway_id":       <gateway_id>
        "geohash":          <most_recent_geohash>
        "sequence_number":  <uint8_t>
    }
}
```

### Audio Frequency Module
GDP Log Name: edu.berkeley.eecs.<signpost_mac_lower>.signpost_audio_frequency.v0-0-1

MQTT Topic: signpost/lab11/audio

Fields represent the average amplitude of the corresponding frequency band in db
over the second following the reported timestamp.

```
{
    "device": "signpost_audio_frequency",
    "timestamp":    <gps_time_as_unix>,
    "63Hz":    <uint8_t>,
    "160Hz":   <uint8_t>,
    "400Hz":   <uint8_t>,
    "1000Hz":  <uint8_t>,
    "2500Hz":  <uint8_t>,
    "6250Hz":  <uint8_t>,
    "16000Hz": <uint8_t>
}

```


### GPS Data
GDP Log Name: edu.berkeley.eecs.<signpost_mac_lower>.signpost_gps.v0-0-1

MQTT Topic: signpost/lab11/gps

```
{
    "device": "signpost_gps",
    "latitude":  <float>,
    "latitude_direction": "N"|"S",
    "longitude": <float>,
    "longitude_direction": "E"|"W",
    "timestamp": <ISO time>
}
```


### Signpost Energy
GDP Log Name: edu.berkeley.eecs.<signpost_mac_lower>.signpost_energy.v0-0-1

MQTT Topic: signpost/lab11/energy

```
{
    "device": "signpost_energy",
    "battery_voltage_mV" <uint16_t>,
    "battery_current_uA" <int32_t>,
    "solar_voltage_mV" <uint16_t>,
    "solar_current_uA" <int32_t>,
    "battery_capacity_percent_remaining" <uint8_t>,
    "battery_capacity_remaining_mAh" <uint16_t>,
    "battery_capacity_full_mAh" <uint16_t>,
    "controller_energy_remaining_mWh" <uint16_t>,
    "module0_energy_remaining_mWh" <uint16_t>,
    "module1_energy_remaining_mWh" <uint16_t>,
    "module2_energy_remaining_mWh" <uint16_t>,
    "module5_energy_remaining_mWh" <uint16_t>,
    "module6_energy_remaining_mWh" <uint16_t>,
    "module7_energy_remaining_mWh" <uint16_t>,
    "controller_energy_average_mWh" <uint16_t>,
    "module0_energy_average_mW" <uint16_t>,
    "module1_energy_average_mW" <uint16_t>,
    "module2_energy_average_mW" <uint16_t>,
    "module5_energy_average_mW" <uint16_t>,
    "module6_energy_average_mW" <uint16_t>,
    "module7_energy_average_mW" <uint16_t>,
}
```

### Radio Status
GDP Log Name: edu.berkeley.eecs.<signpost_mac_lower>.signpost_radio_status.v0-0-1

MQTT Topic: signpost/lab11/radio-status

```
{
    "device": "signpost_radio_status",
    "controller_packets_sent": <uint8_t>,
    "2.4gHz_spectrum_packets_sent": <uint8_t>,
    "ambient_sensing_packets_sent": <uint8_t>,
    "audio_spectrum_packets_sent": <uint8_t>,
    "microwave_radar_packets_sent": <uint8_t>,
    "ucsd_air_quality_packets_sent": <uint8_t>,
    "radio_status_packets_sent": <uint8_t>,
    "radio_queue_length": <uint8_t8_t>
}
```


### Microwave Radar Module

GDP Log Name: edu.berkeley.eecs.<signpost_mac_lower>.signpost_microwave_radar.v0-0-1

MQTT Topic: signpost/lab11/radar

```
{
    "device": "signpost_microwave_radar",
    "motion": <boolean>,
    "velocity_m/s": <float>,
    "motion_confidence": <uint32_t>,
}
```


### Ambient Sensing Module
GDP Log Name: edu.berkeley.eecs.<signpost_mac_lower>.signpost_ambient.v0-0-1

MQTT Topic: signpost/lab11/ambient

```
{
    "device": "signpost_ambient",
    "temperature_c":    <float>,
    "humidity":         <float>,
    "light_lux":        <float>,
    "pressure_pascals": <float>
}
```

### RF Spectrum Sensing Module (currently TV whitespace channels)
GDP Log Name: edu.berkeley.eecs.<signpost_mac_lower>.signpost_rf_spectrum.v0-0-1

MQTT Topic: signpost/lab11/spectrum

```
{
    "device": "signpost_rf_spectrum",
    "470MHz-476MHz_max":    <int8_t>,
    "476MHz-482MHz_max":    <int8_t>,
    .
    .
    .
    "944MHz-950MHz_max":    <int8_t>,
}
```
and

```
{
    "device": "signpost_rf_spectrum",
    "470MHz-476MHz_stddev":    <int8_t>,
    "476MHz-482MHz_stddev":    <int8_t>,
    .
    .
    .
    "944MHz-950MHz_stddev":    <int8_t>,
}
```
and

```
{
    "device": "signpost_rf_spectrum",
    "470MHz-476MHz_mean":    <int8_t>,
    "476MHz-482MHz_mean":    <int8_t>,
    .
    .
    .
    "944MHz-950MHz_mean":    <int8_t>,
}
```

### UCSD Air Quality
GDP Log Name: edu.berkeley.eecs.<signpost_mac_lower>.signpost_ucsd_air_quality.v0-0-1

MQTT Topic: signpost/lab11/aqm

```
{
    "device"              'signpost_ucsd_air_quality',
    "co2_ppm"             <uint16_t>,
    "VOC_PID_ppb"         <uint32_t>,
    "VOC_IAQ_ppb"         <uint32_t>,
    "barometric_millibar" <uint16_t>,
    "humidity_percent"    <uint16_t>,
}
```
<!--

I2C Message Structure
---------------------

### 2.4GHz RF Spectrum Sensing Module

```
18 bytes:

u8 : 0x01
i8 : Channel 11 RSSI
i8 : Channel 12 RSSI
i8 : Channel 13 RSSI
i8 : Channel 14 RSSI
i8 : Channel 15 RSSI
i8 : Channel 16 RSSI
i8 : Channel 17 RSSI
i8 : Channel 18 RSSI
i8 : Channel 19 RSSI
i8 : Channel 20 RSSI
i8 : Channel 21 RSSI
i8 : Channel 22 RSSI
i8 : Channel 23 RSSI
i8 : Channel 24 RSSI
i8 : Channel 25 RSSI
i8 : Channel 26 RSSI
```

### Ambient Sensing Module

```
10 bytes:

u8  : 0x01
u16 : temperature (1/100 degree c)
u16 : humidity (1/100 %)
u16 : light (lux)
u16 : pressure
```


### Controller


Energy & status
```
19 bytes:

u8  : 0x01
u16 : Module0 Energy (mAh)
u16 : Module1 Energy (mAh)
u16 : Module2 Energy (mAh)
u16 : Controller/Backplane Energy (mAh)
u16 : Linux Energy (mAh)
u16 : Module5 Energy (mAh)
u16 : Module6 Energy (mAh)
u16 : Module7 Energy (mAh)
```

GPS
```
18 bytes:

u8  : 0x20
u8  : 0x02
u8  : Day
u8  : Month
u8  : Year (Last two digits)
u8  : Hours
u8  : Minutes
u8  : Seconds
u32 : Latitude
u32 : Longitude
u8  : Fix (1=No Fix, 2=2D, 3=3D)
u8  : Satellite Count (Satellites used in fix)
```


### Microwave Radar Module

```
7 bytes:

u8  : 0x32
u8  : 0x01
u8  : motion since last transmission (boolean)
u32 : max speed measured since last transmission (mm/s)
```

### UCSD Air Quality

```
16 bytes:

u8  : 0x35
u8  : 0x01
u16 : CO2 ppm
u32 : VOC from the PID sensor
u32 : VOC from the IAQ sensor
u16 : Barometric pressure
u16 : Percent Humidity
```

### Radio Status (Over the air format - all energy estimated)

```
16 bytes:

u8  : 0x22
u8  : 0x01
u16 : controller energy packets sent
u16 : controller gps packets sent
u16 : 2.4ghz packets sent
u16 : ambient sensing packets sent
u16 : audio spectrum packets sent
u16 : microwave radar packets sent
u16 : ucsd air quality packets sent
u16 : radio status packets sent
```
-->
