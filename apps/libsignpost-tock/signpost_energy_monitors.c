#include "i2c_selector.h"
#include "ltc294x.h"
#include "signpost_energy_monitors.h"
#include "max17205.h"

static uint8_t module_num_to_selector_mask[8] = {0x4, 0x8, 0x10, 0, 0, 0x20, 0x40, 0x80};
static ltc294x_model_e model;
static uint16_t prescaler;


////////////////////////////////////////////////////////////
// Initialization functions
// Make sure to call the right one
/////////////////////////////////////////////////////////
void signpost_energy_init_ltc2941 (void) {
    // configure each ltc with the correct prescaler
    for (int i = 0; i < 8; i++) {
        i2c_selector_select_channels_sync(1<<i);
        ltc294x_configure_sync(LTC2941, InterruptPinAlertMode, POWER_MODULE_PRESCALER_LTC2941, VbatAlertOff);
    }

    model = LTC2941;
    prescaler = POWER_MODULE_PRESCALER_LTC2941;

    // set all channels open for Alert Response
    i2c_selector_select_channels_sync(0xFF);
}

void signpost_energy_init_ltc2943 (void) {
    // configure each ltc with the correct prescaler
    for (int i = 0; i < 9; i++) {
        i2c_selector_select_channels_sync(1<<i);
        ltc294x_configure_sync(LTC2943, InterruptPinAlertMode, POWER_MODULE_PRESCALER_LTC2943, ADCScan);
    }

    model = LTC2943;
    prescaler = POWER_MODULE_PRESCALER_LTC2943;

    // set all channels open for Alert Response
    i2c_selector_select_channels_sync(0x1FF);
}

////////////////////////////////////////////////
// Static helper functions
////////////////////////////////////////////////
static int get_ltc_energy_uah (int selector_mask, int rsense) {
	// Select correct LTC294x
	i2c_selector_select_channels_sync(selector_mask);

    return ltc294x_convert_to_coulomb_uah(ltc294x_get_charge_sync(),rsense,prescaler,model);
}

static int get_ltc_current_ua (int selector_mask, int rsense) {
	// Select correct LTC294x
    i2c_selector_select_channels_sync(selector_mask);

    return ltc294x_convert_to_current_ua(ltc294x_get_current_sync(),rsense);
}

static void reset_ltc_energy (int selector_mask) {
	// Select correct LTC294x
    i2c_selector_select_channels_sync(selector_mask);

	ltc294x_reset_charge_sync();
}

///////////////////////////////////////////////////
// Energy Reset Functions
///////////////////////////////////////////////////
void signpost_energy_reset_all_energy (void) {
    signpost_energy_reset_controller_energy();
    signpost_energy_reset_solar_energy();
    signpost_energy_reset_linux_energy();
    for(uint8_t i = 0; i < 8; i++) {
        signpost_energy_reset_module_energy(i);
    }
}

void signpost_energy_reset_controller_energy (void) {
    reset_ltc_energy(0x1);
}

void signpost_energy_reset_module_energy (int module_num) {
    if(module_num != 3 && module_num != 4) {
        reset_ltc_energy(module_num_to_selector_mask[module_num]);
    }
}

void signpost_energy_reset_solar_energy (void) {
    reset_ltc_energy(0x100);
}

void signpost_energy_reset_linux_energy (void) {
    reset_ltc_energy(0x2);
}

///////////////////////////////////////////////////
// Raw coulomb counter read functions
// ///////////////////////////////////////////////
uint32_t signpost_energy_get_controller_energy_uwh (void) {
    return get_ltc_energy_uah(0x1,POWER_MODULE_RSENSE_MOHM)*CONTROLLER_VOLTAGE;
}
uint32_t signpost_energy_get_linux_energy_uwh (void) {
	return get_ltc_energy_uah(0x2,POWER_MODULE_RSENSE_MOHM)*LINUX_VOLTAGE;
}
uint32_t signpost_energy_get_solar_energy_uwh (void) {
	return get_ltc_energy_uah(0x100,POWER_MODULE_SOLAR_RSENSE_MOHM)*SOLAR_VOLTAGE;
}
uint32_t signpost_energy_get_module_energy_uwh (int module_num) {
	return get_ltc_energy_uah(module_num_to_selector_mask[module_num],POWER_MODULE_RSENSE_MOHM)*MODULE_VOLTAGE;
}

////////////////////////////////////////////////////
// Raw current read functions
////////////////////////////////////////////////////

uint32_t signpost_energy_get_controller_current_ua (void) {
    return get_ltc_current_ua(0x1, POWER_MODULE_RSENSE_MOHM);
}

uint32_t signpost_energy_get_linux_current_ua (void) {
    return get_ltc_current_ua(0x2, POWER_MODULE_RSENSE_MOHM);
}

uint32_t signpost_energy_get_module_current_ua (int module_num) {
    return get_ltc_current_ua(module_num_to_selector_mask[module_num],POWER_MODULE_RSENSE_MOHM);
}

uint32_t signpost_energy_get_solar_current_ua (void) {
    return get_ltc_current_ua(0x100,POWER_MODULE_SOLAR_RSENSE_MOHM);
}

int32_t signpost_energy_get_battery_current_ua (void) {
    uint16_t voltage;
    int16_t current;
    max17205_read_voltage_current_sync(&voltage,&current);
    float c = max17205_get_current_uA(current);
    return (int32_t)c;
}


///////////////////////////////////////////////
// More information about the battery
///////////////////////////////////////////////
uint32_t signpost_energy_get_battery_capacity_uwh (void) {
    uint16_t percent;
    uint16_t charge;
    uint16_t full;
    max17205_read_soc_sync(&percent, &charge, &full);
    return max17205_get_capacity_uAh(full)*BATTERY_VOLTAGE_NOM;
}

uint32_t signpost_energy_get_battery_percent_mp (void) {
    uint16_t percent;
    uint16_t charge;
    uint16_t full;
    max17205_read_soc_sync(&percent, &charge, &full);
    return max17205_get_percentage_mP(percent);
}

uint32_t signpost_energy_get_battery_energy_uwh (void) {
    uint16_t percent;
    uint16_t charge;
    uint16_t full;
    max17205_read_soc_sync(&percent, &charge, &full);
    return max17205_get_capacity_uAh(charge)*BATTERY_VOLTAGE_NOM;
}


///////////////////////////////////////////////////////////
// These functions give you voltages for battery and solar
///////////////////////////////////////////////////////////
uint16_t signpost_energy_get_battery_voltage_mv (void) {
    uint16_t voltage;
    int16_t current;
    max17205_read_voltage_current_sync(&voltage,&current);
    float v = max17205_get_voltage_mV(voltage);
    return (int)v;
}


uint16_t signpost_energy_get_solar_voltage_mv (void) {
    //selector #3 slot 1
    i2c_selector_select_channels_sync(0x100);

    return ltc294x_convert_to_voltage_mv(ltc294x_get_voltage_sync());
}
