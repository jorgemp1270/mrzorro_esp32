#include <Arduino.h>

// =================================================================================
// SELECCIÓN DE FIRMWARE
// Descomenta la línea correspondiente al dispositivo que deseas programar.
// =================================================================================

// OPCIÓN A: ESP32 A (Capturador de Audio - NodeMCU-32S)
// - Conectado al Micrófono INMP441
// - Envía audio por UART al ESP32 B
// #define FIRMWARE_A_CAPTURE

// OPCIÓN B: ESP32 B (Procesador - LilyGo T-SIM7000G)
// - Conectado a SD, WiFi, Bocina MAX98357A
// - Recibe audio por UART, guarda en SD, envía a Backend
#define FIRMWARE_B_PROCESSOR

// =================================================================================

#if defined(FIRMWARE_A_CAPTURE) && defined(FIRMWARE_B_PROCESSOR)
#error "Solo puedes seleccionar un firmware a la vez. Comenta una de las opciones."
#endif

#if !defined(FIRMWARE_A_CAPTURE) && !defined(FIRMWARE_B_PROCESSOR)
#error "Debes seleccionar un firmware (A o B) descomentando una de las opciones."
#endif

#ifdef FIRMWARE_A_CAPTURE
#include "esp32_A.h"
#endif

#ifdef FIRMWARE_B_PROCESSOR
#include "esp32_B.h"
#endif
