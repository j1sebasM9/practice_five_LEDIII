#include "adc_sensors.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include <stdio.h>

#define LDR_PIN 26
#define BAT_PIN 27

#define BAT_DIVIDER_FACTOR 3.2f 
#define ADC_VREF           3.3f
#define ADC_MAX_VAL        4095.0f

#define LDR_RAW_OSCURIDAD  3673.0f 
#define LDR_RAW_LUZ_10CM   248.0f  

AdcData_t current_adc_data = {0};

// === NUEVA VARIABLE PARA EL FILTRO ===
static float bateria_filtrada = -1.0f; // Inicia en negativo para saber que está vacía

void adc_sensors_init(void) {
    adc_init();
    adc_gpio_init(LDR_PIN);
    adc_gpio_init(BAT_PIN);
    printf("[ADC OK] Modulo analogico inicializado.\n");
}

void adc_sensors_read(void) {
    uint32_t adc_bat_acum = 0;
    uint32_t adc_ldr_acum = 0;
    
    // Sobremuestreo básico
    for(int i=0; i<16; i++) {
        adc_select_input(1); adc_bat_acum += adc_read(); 
        adc_select_input(0); adc_ldr_acum += adc_read(); 
    }
    
    float raw_bat_actual = (float)(adc_bat_acum / 16);

    // =========================================================
    // FILTRO EXPONENCIAL (Elimina la oscilación de 75% - 82%)
    // =========================================================
    if (bateria_filtrada < 0.0f) {
        bateria_filtrada = raw_bat_actual; // Primera lectura en seco
    } else {
        // Le creemos 80% al historial pasado y solo 20% a la lectura nueva (Suavizado)
        bateria_filtrada = (bateria_filtrada * 0.8f) + (raw_bat_actual * 0.2f);
    }

    current_adc_data.raw_bateria = (uint16_t)bateria_filtrada;
    current_adc_data.raw_luz = (uint16_t)(adc_ldr_acum / 16);
    
    // Cálculos para la Batería
    float v_pin = ((float)current_adc_data.raw_bateria * ADC_VREF) / ADC_MAX_VAL;
    current_adc_data.voltaje_bateria = v_pin * BAT_DIVIDER_FACTOR; 
    
    float porcentaje_bat = ((current_adc_data.voltaje_bateria - 3.2f) / (3.7f - 3.2f)) * 100.0f;
    if (porcentaje_bat > 100.0f) porcentaje_bat = 100.0f;
    if (porcentaje_bat < 0.0f)   porcentaje_bat = 0.0f;
    current_adc_data.porcentaje_bateria = porcentaje_bat; 

    // Cálculos para la Luz
    float raw_luz_f = (float)current_adc_data.raw_luz;
    float porcentaje_luz = ((LDR_RAW_OSCURIDAD - raw_luz_f) / (LDR_RAW_OSCURIDAD - LDR_RAW_LUZ_10CM)) * 100.0f;
    
    if (porcentaje_luz > 100.0f) porcentaje_luz = 100.0f;
    if (porcentaje_luz < 0.0f)   porcentaje_luz = 0.0f;
    current_adc_data.porcentaje_luz = porcentaje_luz; 
}