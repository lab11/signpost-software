Current Signpost Data Schemas
=====================

These are the data and associated data formats that are currently being
collected by deployed Signposts and the modules that we have deployed with them. 
This data is being sent to the internal
Lab11 Influx database, the Lab11 TimescaleDB.
and can be subscribed to using MQTT with the appropriate credentials. 
Please contact us to set up an
MQTT username and password.

### Stream Naming

Module streams are formated as: signpost/mac\_address/org\_name/module\_name/topic\_name
(i.e. signpost/c098e5120000/lab11/audio/spectrum)

Generic streams useful to all modules belong to the "signpost" organization.
(i.e. signpost/c098e5120000/signpost/control/gps)

### Deployed Signposts

All signpost will report energy, gps, and radio status data.
Signpost will also report data based on their currently installed modules.
Installed and working modules are listed below next to the signpost mac address.

Currently the following signposts are deployed:
  - c098e5120001 (audio, rf spectrum, ambient) - North Campus
  - c098e5120003 (audio, rf spectrum, ambient) - North Campus
  - c098e5120015 (audio, rf spectrum, ambient) - North Campus
  - c098e5120016 (audio, rf spectrum, ambient) - North Campus
  - c098e5120017 (audio, rf spectrum, ambient) - North Campus
  - c098e5120018 (audio, rf spectrum, ambient) - North Campus
  - c098e512001a (audio, rf spectrum, ambient) - North Campus
  - c098e512001b (audio, rf spectrum, ambient) - North Campus
  - c098e512001c (audio, rf spectrum, ambient) - North Campus
  - c098e512001d (audio, rf spectrum, ambient) - North Campus
  - c098e512001e (audio, rf spectrum, ambient) - North Campus
  - c098e512001f (audio, rf spectrum, ambient) - North Campus

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
MQTT Topic: (i.e. signpost/mac\_address/lab11/audio/spectrum)

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
MQTT Topic: signpost/mac\_address/signpost/control/gps

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
MQTT Topic: signpost/mac\_address/signpost/control/energy

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
MQTT Topic: signpost/mac\_address/signpost/radio/status

```
{
    "device": "signpost_radio",
    "packets_sent": {
        "module_name": <uint8_t>,
        "module_name": <uint8_t>,
        .
        .
        .
    },
    "packets_delayed_for_muling": {
        "module_name": <uint16_t>,
        "module_name": <uint16_t>,
        .
        .
        .
    }
    "radio_queue_length": <uint8_t>
}
```


### Microwave Radar Module
MQTT Topic: signpost/mac\_address/lab11/radar/motion

```
{
    "device": "signpost_microwave_radar",
    "motion": <boolean>,
    "velocity_m/s": <float>,
    "motion_confidence": <uint32_t>,
}
```


### Ambient Sensing Module
MQTT Topic: signpost/mac\_address/lab11/ambient/tphl

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
MQTT Topic: signpost/mac\_address/lab11/spectrum/ws_max

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

MQTT Topic: signpost/mac\_address/lab11/spectrum/ws_stddev

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

MQTT Topic: signpost/mac\_address/lab11/spectrum/ws_mean

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
