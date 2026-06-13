#ifndef ADC_SENSORS_H
#define ADC_SENSORS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t raw_bateria;    // BAT_RAW
    float voltaje_bateria;   // BAT_V
    float porcentaje_bateria;// BAT_PCT
    uint16_t raw_luz;        // LIGHT_RAW
    float porcentaje_luz;    // LIGHT_PCT
} AdcData_t;

extern AdcData_t current_adc_data;

void adc_sensors_init(void);
void adc_sensors_read(void);

#endif