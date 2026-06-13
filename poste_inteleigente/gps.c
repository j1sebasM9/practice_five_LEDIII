#include "gps.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h> // Necesario para atof()

#define UART_GPS       uart1
#define BAUD_RATE      9600
#define GPS_TX_PIN     4
#define GPS_RX_PIN     5
#define BUFFER_SIZE    128

static volatile char rx_buffer[BUFFER_SIZE];
static volatile uint8_t rx_index = 0;

// 1. CORRECCIÓN: Inicialización completa de los 5 campos del Struct
GpsData_t current_gps_data = {0.0f, 0.0f, 0.0f, 0, false, ""};

// Función auxiliar: Convierte NMEA (DDMM.MMMM) a Grados Decimales (DD.DDDD)
float nmea_to_decimal(float nmea_coord, char direction) {
    if (nmea_coord == 0.0) return 0.0; // Protección contra ceros
    int degrees = (int)(nmea_coord / 100); 
    float minutes = nmea_coord - (degrees * 100); 
    float decimal = degrees + (minutes / 60.0); 
    
    if (direction == 'S' || direction == 'W') {
        decimal = -decimal;
    }
    return decimal;
}

// ISR: Recepción de datos crudos (No bloqueante)
void on_uart_gps_rx() {
    while (uart_is_readable(UART_GPS)) {
        char c = uart_getc(UART_GPS);
        if (!gFlags.B.gpsReady) { 
            if (c == '\n') {
                rx_buffer[rx_index] = '\0';
                gFlags.B.gpsReady = true;
                rx_index = 0;
            } else if (c != '\r' && rx_index < BUFFER_SIZE - 1) {
                rx_buffer[rx_index++] = c;
            }
        }
    }
}

void gps_init(void) {
    uart_init(UART_GPS, BAUD_RATE);
    gpio_set_function(GPS_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(GPS_RX_PIN, GPIO_FUNC_UART);
    irq_set_exclusive_handler(UART1_IRQ, on_uart_gps_rx);
    irq_set_enabled(UART1_IRQ, true);
    uart_set_irq_enables(UART_GPS, true, false);
}

void gps_process(void) {
    if (strncmp((char*)rx_buffer, "$GPGGA", 6) == 0 || strncmp((char*)rx_buffer, "$GNGGA", 6) == 0) {
        char temp_buffer[BUFFER_SIZE];
        strcpy(temp_buffer, (char*)rx_buffer);
        
        char *tokens[20];
        int token_count = 0;
        char *p = temp_buffer;
        
        tokens[token_count++] = p;
        while (*p && token_count < 20) {
            if (*p == ',') {
                *p = '\0';
                tokens[token_count++] = p + 1;
            }
            p++;
        }

// Verificamos tokens y extraemos (El token 9 es la Altitud en MSL)
        int calidad_fix = (token_count > 6) ? atoi(tokens[6]) : 0;
        int satelites_leidos = (token_count > 7) ? atoi(tokens[7]) : 0;
        float altitud_leida = (token_count > 9) ? atof(tokens[9]) : 0.0f;

        if (token_count > 9 && calidad_fix > 0) {
            current_gps_data.latitud = nmea_to_decimal(atof(tokens[2]), tokens[3][0]);
            current_gps_data.longitud = nmea_to_decimal(atof(tokens[4]), tokens[5][0]);
            current_gps_data.altitud = altitud_leida;
            current_gps_data.satelites = satelites_leidos;
            current_gps_data.fix_valido = true;
        } else {
            current_gps_data.latitud = 0.0;
            current_gps_data.longitud = 0.0;
            current_gps_data.altitud = 0.0;
            current_gps_data.satelites = satelites_leidos; 
            current_gps_data.fix_valido = false;
        }
    }
    gFlags.B.gpsReady = false; 
}