#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// qlsb = 0.0625mAh with 0.017 Ohm sense resistor
#define POWER_MODULE_PRESCALER_LTC2941 32
#define POWER_MODULE_PRESCALER_LTC2943 64

//in Mohm
#define POWER_MODULE_RSENSE 17
#define POWER_MODULE_SOLAR_RSENSE 50

//voltages for the energy numbers
#define MODULE_VOLTAGE 5
#define LINUX_VOLTAGE 5
#define CONTROLLER_VOLTAGE 3.3
#define BATTERY_VOLTAGE_NOM 11.1

//initialize based on the version of the monitors that we are using
void signpost_energy_init_ltc2941 (void);
void signpost_energy_init_ltc2943 (void);

//this zeros the coulomb counters for each counter
void signpost_energy_reset_all_energy (void);
void signpost_energy_reset_controller_energy (void);
void signpost_energy_reset_linux_energy (void);
void signpost_energy_reset_solar_energy (void);
void signpost_energy_reset_module_energy (int module_num);

//these functions return the energy used in uWh by reading
//the coulomb counters and multiplying by the voltage
//Note that energy cannot flow from modules back into the battery
//So we use unsigned energy types here
uint32_t signpost_energy_get_controller_energy (void);
uint32_t signpost_energy_get_linux_energy (void);
uint32_t signpost_energy_get_module_energy (int module_num);

//these function return the instantaneous current in uA
//Battery current can be positive or negative
int32_t signpost_energy_get_battery_current (void);
uint32_t signpost_energy_get_solar_current (void);
uint32_t signpost_energy_get_controller_current (void);
uint32_t signpost_energy_get_linux_current (void);
uint32_t signpost_energy_get_module_current (int module_num);

//uWh
uint32_t signpost_energy_get_battery_capacity (void);
//whole number percent
uint32_t signpost_energy_get_battery_percent (void);
//uWh
uint32_t signpost_energy_get_battery_energy (void);

//these functions return instantaneous current for Voltage
//These are returned in mV
uint16_t signpost_energy_get_battery_voltage (void);
uint16_t signpost_energy_get_solar_voltage (void);

#ifdef __cplusplus
}
#endif
