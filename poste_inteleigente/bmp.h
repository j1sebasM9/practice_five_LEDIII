#ifndef BMP_H
#define BMP_H

#include <stdint.h>
#include <stdbool.h>

// Estructura de los datos del sensor
typedef struct {
    float temperatura; // En grados Centígrados
    float presion;     // En hectoPascales (hPa)
    float altitud;     // En metros sobre el nivel del mar
    bool leido_ok;
} BmpData_t;

extern BmpData_t current_bmp_data;

void bmp_init(void);
void bmp_read(void);

#endif