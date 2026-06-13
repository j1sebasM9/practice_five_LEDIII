#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

// NUEVAS LIBRERÍAS DE HARDWARE PURO:
#include "hardware/sync.h"
#include "hardware/structs/scb.h"

#include "hardware/rtc.h"
#include "hardware/uart.h" 
#include "hardware/clocks.h"
#include "gps.h"
#include "bmp.h"
#include "adc_sensors.h"

#define UART_ZIGBEE uart0
#define ZIGBEE_BAUD_RATE 9600
#define LED_INTERNO 25

volatile sysFlags_t gFlags;

typedef enum { MODE_ACTIVE, MODE_SAVE } SystemMode_t;
SystemMode_t current_mode = MODE_ACTIVE;

// === NUEVA VARIABLE: Evita que el nodo se despierte si la base le ordenó dormir ===
bool forzado_por_base = false; 

// ==============================================================
// 1. MANEJO DE ENERGÍA Y RTC (DORMANT MANUAL VÍA REGISTROS)
// ==============================================================
void rtc_sleep_callback(void) {}
void ir_a_dormir_save() {
    datetime_t t;
    rtc_get_datetime(&t);
    
    // Configurar alarma para despertar en 5 segundos
    t.sec += 5;
    if (t.sec >= 60) { t.sec -= 60; t.min += 1; }
    if (t.min >= 60) { t.min -= 60; t.hour += 1; }
    // Ajuste de seguridad por si el cambio de hora afecta el día
    if (t.hour >= 24) { t.hour -= 24; t.day += 1; } 
    
    // 1. Programar la alarma en el hardware RTC
    rtc_set_alarm(&t, &rtc_sleep_callback);
    
    // --- LA SOLUCIÓN MAGICA AQUÍ ---
    // Apagamos la interrupción del GPS para que sus datos no rompan el sueño
    irq_set_enabled(UART1_IRQ, false);

    // 2. Escribir bit SLEEPDEEP directo en el System Control Block de ARM
    scb_hw->scr |= M0PLUS_SCR_SLEEPDEEP_BITS;
    
    // 3. Detener procesador. ¡Ahora SÍ dormirá los 5 segundos completos!
    __wfi(); 
    
    // --- AL DESPERTAR ---
    // 4. Limpiar el bit SLEEPDEEP para regresar a modo normal
    scb_hw->scr &= ~M0PLUS_SCR_SLEEPDEEP_BITS;

    // 5. Volver a encender la interrupción del GPS para seguir actualizando lat/lon
    irq_set_enabled(UART1_IRQ, true);
}

// ==============================================================
// 2. RECEPCIÓN DE COMANDOS DESDE LA BASE (INTERRUPCIÓN UART)
// ==============================================================
static volatile bool comando_recibido = false;
static char cmd_buffer[64];
static volatile uint8_t cmd_index = 0;

void on_zigbee_rx() {
    while (uart_is_readable(UART_ZIGBEE)) {
        char c = uart_getc(UART_ZIGBEE);
        if (c == '\n' || c == '\r' || c == '#') {
            cmd_buffer[cmd_index] = '\0';
            comando_recibido = true;
            cmd_index = 0;
        } else if (cmd_index < 63) {
            cmd_buffer[cmd_index++] = c;
        }
    }
}

// ==============================================================
// 3. RUTINA PRINCIPAL
// ==============================================================
int main() {
    stdio_init_all();
    gpio_init(LED_INTERNO); gpio_set_dir(LED_INTERNO, GPIO_OUT);
    
    // Inicializar RTC (Base de tiempo real para el DORMANT)
    datetime_t t = { .year  = 2026, .month = 6, .day   = 3, .dotw  = 3, .hour  = 12, .min   = 0, .sec   = 0 };
    rtc_init(); rtc_set_datetime(&t);

    uart_init(UART_ZIGBEE, ZIGBEE_BAUD_RATE);
    gpio_set_function(0, GPIO_FUNC_UART); gpio_set_function(1, GPIO_FUNC_UART);
    
    // Habilitar interrupción de recepción UART0
    irq_set_exclusive_handler(UART0_IRQ, on_zigbee_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(UART_ZIGBEE, true, false);
    
    gps_init(); bmp_init(); adc_sensors_init();

    printf("\n[NODO REMOTO] Arquitectura Sincrona DORMANT Inicializada.\n");

    uint32_t last_sensor_read = time_us_32();

    while (true) {
        uint32_t current_time = time_us_32();

        // 1. Procesar GPS si hay datos
        if (gFlags.B.gpsReady) gps_process();

        // 2. Procesar solicitudes de la Base
        if (comando_recibido) {
            if (strstr(cmd_buffer, "REQ:DATA")) {
                bmp_read(); 
                adc_sensors_read();
                
                char trama_final[256];
                int offset = 0;

                // 1. Cabecera y Modo
                offset += snprintf(trama_final + offset, sizeof(trama_final) - offset, 
                                   "RES:DATA,MODE=%s,", 
                                   current_mode == MODE_ACTIVE ? "ACTIVE" : "SAVE");

                // 2. Sensores Atmosféricos (Manejo estricto de error para la Base)
                if (current_bmp_data.leido_ok) {
                    offset += snprintf(trama_final + offset, sizeof(trama_final) - offset, 
                                       "TEMP_C=%.1f,PRESS_HPA=%.1f,", 
                                       current_bmp_data.temperatura, current_bmp_data.presion);
                } else {
                    offset += snprintf(trama_final + offset, sizeof(trama_final) - offset, 
                                       "TEMP_C=ERR,PRESS_HPA=ERR,");
                }

                // 3. Sensores Analógicos (Luz y Batería)
                offset += snprintf(trama_final + offset, sizeof(trama_final) - offset, 
                                   "LIGHT_RAW=%u,LIGHT_PCT=%.1f,BAT_V=%.2f,BAT_PCT=%.1f,BAT_RAW=%u,", 
                                   current_adc_data.raw_luz, current_adc_data.porcentaje_luz, 
                                   current_adc_data.voltaje_bateria, current_adc_data.porcentaje_bateria, current_adc_data.raw_bateria);

                // 4. Datos del GPS
                if (current_gps_data.fix_valido) {
                    offset += snprintf(trama_final + offset, sizeof(trama_final) - offset, 
                                       "LAT=%.6f,LON=%.6f,ALT_M=%.1f,FIX=1,SAT=%d", 
                                       current_gps_data.latitud, current_gps_data.longitud, 
                                       current_gps_data.altitud, current_gps_data.satelites);
                } else {
                    // Manda todo en ceros, pero respeta el formato exigido por la Base
                    offset += snprintf(trama_final + offset, sizeof(trama_final) - offset, 
                                       "LAT=0.000000,LON=0.000000,ALT_M=0.0,FIX=0,SAT=%d", 
                                       current_gps_data.satelites);
                }

                // 5. Enviar por UART
                uart_puts(UART_ZIGBEE, trama_final);
                uart_puts(UART_ZIGBEE, "\n"); // MUY IMPORTANTE: La base necesita este 'Enter' para saber que la trama terminó
            }
            // === LÓGICA CORREGIDA PARA BLOQUEAR EL MODO SAVE ===
            else if (strstr(cmd_buffer, "REQ:MODE_SAVE")) {
                current_mode = MODE_SAVE;
                forzado_por_base = true; // El usuario lo pidió, ignoramos si la batería está alta
            }
            else if (strstr(cmd_buffer, "REQ:MODE_ACTIVE")) {
                current_mode = MODE_ACTIVE;
                forzado_por_base = false; // Liberamos el bloqueo
            }
            
            comando_recibido = false;
        }

        // 3. Máquina de Estados de Energía
        if (current_mode == MODE_ACTIVE) {
            if (current_time - last_sensor_read > 1000000) {
                adc_sensors_read(); // Chequeo de batería
                if (current_adc_data.porcentaje_bateria < 20.0f) current_mode = MODE_SAVE;
                last_sensor_read = current_time;
                gpio_xor_mask(1u << LED_INTERNO); // Parpadea el LED a 1 segundo
            }
            // MODO SLEEP (Cumplimiento de la guía):
            __wfi(); 
        } 
        else if (current_mode == MODE_SAVE) {
            // Avisar a la base que estamos despiertos por si tiene algo en cola
            uart_puts(UART_ZIGBEE, "HB:AWAKE\n");
            
            // === VENTANA DE ESPERA CORREGIDA (No bloqueante, sin sleep_ms) ===
            uint32_t t_wake = time_us_32();
            while(time_us_32() - t_wake < 500000) {
                if(comando_recibido) break; // Si entra comando de la base, rompemos rápido sin esperar los 500ms
            }
            if (comando_recibido) {
                continue; 
            }
            adc_sensors_read();

            // === TRANSICIÓN CORREGIDA: Solo vuelve a ACTIVE si la batería está bien Y la base no lo forzó ===
            if (current_adc_data.porcentaje_bateria >= 25.0f && !forzado_por_base) {
                current_mode = MODE_ACTIVE;
            } 
            else {
                gpio_put(LED_INTERNO, 1);
                sleep_ms(20); 
                gpio_put(LED_INTERNO, 0);
                ir_a_dormir_save(); // MODO DORMANT REAL
            }
            last_sensor_read = time_us_32();
        }
    }
}