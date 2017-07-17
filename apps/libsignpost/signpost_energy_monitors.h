#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// qlsb = 0.0625mAh with 0.017 Ohm sense resistor
#define POWER_MODULE_PRESCALER_LTC2941 32
#define POWER_MODULE_PRESCALER_LTC2943 64

#define POWER_MODULE_RSENSE_MOHM 17
#define POWER_MODULE_SOLAR_RSENSE_MOHM 50

// Voltages for the energy numbers
#define MODULE_VOLTAGE 5
#define LINUX_VOLTAGE 5
#define CONTROLLER_VOLTAGE 3.3
#define BATTERY_VOLTAGE_NOM 11.1

// Initialize based on the version of the monitors that we are using
void signpost_energy_init_ltc2941 (void);
void signpost_energy_init_ltc2943 (void);

// This zeros the coulomb counters for each counter
void signpost_energy_reset_all_energy (void);
void signpost_energy_reset_controller_energy (void);
void signpost_energy_reset_linux_energy (void);
void signpost_energy_reset_solar_energy (void);
void signpost_energy_reset_module_energy (int module_num);

// These functions return the energy used in uWh by reading
// the coulomb counters and multiplying by the voltage
// Note that energy cannot flow from modules back into the battery
// so we use unsigned energy types here
uint32_t signpost_energy_get_controller_energy_uwh (void);
uint32_t signpost_energy_get_linux_energy_uwh (void);
uint32_t signpost_energy_get_module_energy_uwh (int module_num);

// These function return the instantaneous current in uA
// Battery current can be positive or negative
int32_t signpost_energy_get_battery_current_ua (void);
uint32_t signpost_energy_get_solar_current_ua (void);
uint32_t signpost_energy_get_controller_current_ua (void);
uint32_t signpost_energy_get_linux_current_ua (void);
uint32_t signpost_energy_get_module_current_ua (int module_num);

uint32_t signpost_energy_get_battery_capacity_uwh (void);
uint32_t signpost_energy_get_battery_percent_mp (void);
uint32_t signpost_energy_get_battery_energy_uwh (void);

// These functions return instantaneous current for Voltage
uint16_t signpost_energy_get_battery_voltage_mv (void);
uint16_t signpost_energy_get_solar_voltage_mv (void);

#ifdef __cplusplus
}
#endif
