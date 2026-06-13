#ifndef GPS_H
#define GPS_H

#include <stdint.h>
#include <stdbool.h>

// 1. Estructura de banderas para la Máquina de Estados
typedef union {
    uint8_t W;
    struct {
        bool gpsReady    :1;
        bool zigbeeReady :1;
        bool adcReady    :1;
        bool timerTick   :1;
        uint8_t          :4; 
    } B;
} sysFlags_t;

extern volatile sysFlags_t gFlags;

// 2. Estructura de telemetría consolidada (Con ALT_M)
typedef struct {
    float latitud;
    float longitud;
    float altitud;       // <- Aquí está la variable que el compilador no encontraba
    int satelites;       
    bool fix_valido;
    char payload[128];   
} GpsData_t;

extern GpsData_t current_gps_data;

void gps_init(void);
void gps_process(void);

#endif