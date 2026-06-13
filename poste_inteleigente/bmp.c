#include "bmp.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <stdio.h>
#include <math.h>

#define I2C_PORT i2c0
#define BMP_SDA  16
#define BMP_SCL  17

// Dirección I2C del BMP280 (Suele ser 0x76 en los módulos genéricos, o 0x77)
#define BMP_ADDR 0x76 

BmpData_t current_bmp_data = {0.0, 0.0, 0.0, false};

// Variables de calibración de fábrica (Únicas de cada chip)
uint16_t dig_T1; int16_t dig_T2, dig_T3;
uint16_t dig_P1; int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
int32_t t_fine;

// Función interna para escribir un registro
void bmp_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_write_blocking(I2C_PORT, BMP_ADDR, buf, 2, false);
}

void bmp_init(void) {
    // 1. Iniciar I2C a 400 kHz
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(BMP_SDA, GPIO_FUNC_I2C);
    gpio_set_function(BMP_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(BMP_SDA);
    gpio_pull_up(BMP_SCL);

    // 2. Verificar si el chip responde (Registro de ID = 0xD0, debe dar 0x58)
    uint8_t id_reg = 0xD0;
    uint8_t chip_id;
    i2c_write_blocking(I2C_PORT, BMP_ADDR, &id_reg, 1, true);
    i2c_read_blocking(I2C_PORT, BMP_ADDR, &chip_id, 1, false);

    if (chip_id != 0x58) {
        printf("[BMP280 ERROR] Chip no detectado. Revisa los cables.\n");
        return;
    }

    // 3. Leer los 24 bytes de calibración de fábrica (Registros 0x88 a 0xA1)
    uint8_t calib_reg = 0x88;
    uint8_t cal[24];
    i2c_write_blocking(I2C_PORT, BMP_ADDR, &calib_reg, 1, true);
    i2c_read_blocking(I2C_PORT, BMP_ADDR, cal, 24, false);

    dig_T1 = (cal[1] << 8) | cal[0];
    dig_T2 = (cal[3] << 8) | cal[2];
    dig_T3 = (cal[5] << 8) | cal[4];
    dig_P1 = (cal[7] << 8) | cal[6];
    dig_P2 = (cal[9] << 8) | cal[8];
    dig_P3 = (cal[11] << 8) | cal[10];
    dig_P4 = (cal[13] << 8) | cal[12];
    dig_P5 = (cal[15] << 8) | cal[14];
    dig_P6 = (cal[17] << 8) | cal[16];
    dig_P7 = (cal[19] << 8) | cal[18];
    dig_P8 = (cal[21] << 8) | cal[20];
    dig_P9 = (cal[23] << 8) | cal[22];

    // 4. Encender el sensor (Modo Normal, Oversampling x1 para Temp y Presión)
    bmp_write_reg(0xF4, 0x27);
    
    printf("[BMP280 OK] Sensor inicializado y calibrado.\n");
    current_bmp_data.leido_ok = true;
}

void bmp_read(void) {
    if (!current_bmp_data.leido_ok) return;

    // Leer 6 bytes de datos crudos (0xF7 a 0xFC)
    uint8_t data_reg = 0xF7;
    uint8_t raw[6];
    i2c_write_blocking(I2C_PORT, BMP_ADDR, &data_reg, 1, true);
    i2c_read_blocking(I2C_PORT, BMP_ADDR, raw, 6, false);

    int32_t adc_P = (raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_T = (raw[3] << 12) | (raw[4] << 4) | (raw[5] >> 4);

    // --- Compensación de Temperatura (Fórmula Bosch) ---
    int32_t var1_t, var2_t, T;
    var1_t = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    var2_t = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    t_fine = var1_t + var2_t;
    T = (t_fine * 5 + 128) >> 8;
    current_bmp_data.temperatura = T / 100.0f;

    // --- Compensación de Presión (Fórmula Bosch) ---
    int64_t var1_p, var2_p, p;
    var1_p = ((int64_t)t_fine) - 128000;
    var2_p = var1_p * var1_p * (int64_t)dig_P6;
    var2_p = var2_p + ((var1_p * (int64_t)dig_P5) << 17);
    var2_p = var2_p + (((int64_t)dig_P4) << 35);
    var1_p = ((var1_p * var1_p * (int64_t)dig_P3) >> 8) + ((var1_p * (int64_t)dig_P2) << 12);
    var1_p = (((((int64_t)1) << 47) + var1_p)) * ((int64_t)dig_P1) >> 33;
    
    if (var1_p != 0) {
        p = 1048576 - adc_P;
        p = (((p << 31) - var2_p) * 3125) / var1_p;
        var1_p = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
        var2_p = (((int64_t)dig_P8) * p) >> 19;
        p = ((p + var1_p + var2_p) >> 8) + (((int64_t)dig_P7) << 4);
        
        current_bmp_data.presion = (float)p / 25600.0f; // Convertido a hectoPascales (hPa)
        
        // --- Cálculo de Altitud (Atmósfera Estándar) ---
        // Fórmula: 44330 * (1 - (P/P0)^(1/5.255))
        current_bmp_data.altitud = 44330.0f * (1.0f - pow(current_bmp_data.presion / 1013.25f, 0.1903f));
    }
}