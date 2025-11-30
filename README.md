# 🦊 Mr. Zorro - Asistente de Voz ESP32 (Dual Core Architecture)

Este proyecto implementa un asistente de voz utilizando **dos microcontroladores ESP32** trabajando en conjunto para superar problemas de latencia de red.

*   **ESP32 A (Capturador):** Se encarga exclusivamente de grabar audio de alta calidad y enviarlo al procesador.
*   **ESP32 B (Procesador):** Recibe el audio, lo almacena en una tarjeta SD, lo envía al backend para procesamiento y reproduce la respuesta.

Se integra con una aplicación móvil a través de Bluetooth Low Energy (BLE) para la configuración y autenticación.

## 🪁 Demostración

<div align="center">
  <video src=".resources/videos/demo.mp4" width="80%" controls></video>
  <br>
  <a href=".resources/videos/demo.mp4">Ver video de demostración</a>
</div>

## 📄 Documentación del proyecto

- **Documentación técnica**: [Mr. Zorro](.resources/docs/Documentacion_MrZorro.pdf)

## 🔗 Repositorios

Este proyecto es parte de un ecosistema más grande:
- **Flutter APP**: [mrzorro_app](https://github.com/jorgemp1270/mrzorro_app)
- **Backend API**: [mrzorro_api](https://github.com/jorgemp1270/mrzorro_api)

## Arquitectura del Sistema

Debido a la latencia de la red, el sistema se divide en dos módulos conectados por UART de alta velocidad:

1.  **Módulo de Captura (ESP32 A):**
    *   Lee datos del micrófono I2S (INMP441).
    *   Envía el audio en tiempo real vía UART al ESP32 B.
    *   Gestiona el botón de grabación y el LED de estado.

2.  **Módulo de Procesamiento (ESP32 B):**
    *   Recibe el audio por UART y lo guarda en una **Tarjeta SD**.
    *   Se conecta a WiFi y gestiona la comunicación BLE con la App.
    *   Sube el archivo de audio al servidor Backend.
    *   Descarga la respuesta y la reproduce por el altavoz I2S (MAX98357A).

## Requisitos de Hardware

### ESP32 A (Capturador - NodeMCU-32S)
*   **Microcontrolador:** ESP32 NodeMCU-32S
*   **Micrófono:** INMP441 (I2S)
*   **Botón:** Pulsador
*   **LED:** Indicador de estado

### ESP32 B (Procesador - LilyGo T-SIM7000G o similar)
*   **Microcontrolador:** ESP32 (LilyGo T-SIM7000G usado en este ejemplo)
*   **Almacenamiento:** Módulo MicroSD
*   **Audio:** Amplificador MAX98357A + Altavoz
*   **Conectividad:** WiFi + BLE

## Conexiones y Pines

### 1. Conexión entre ESP32 A y ESP32 B (UART)
Para comunicar los dos microcontroladores, conecta sus pines UART cruzados y comparte la tierra (GND).

| ESP32 A (Capturador) | ESP32 B (Procesador) | Función |
| :--- | :--- | :--- |
| GPIO 17 (TX) | GPIO 27 (RX) | Envío de Audio |
| GPIO 16 (RX) | GPIO 26 (TX) | (Opcional) Retorno |
| GND | GND | **IMPORTANTE: Tierra común** |

### 2. Pines ESP32 A (Capturador)

| Componente | Pin | GPIO |
| :--- | :--- | :--- |
| **INMP441** | SCK | 26 |
| | WS | 25 |
| | SD | 33 |
| **Botón** | Pin 1 | 27 |
| **LED** | Ánodo | 2 |

### 3. Pines ESP32 B (Procesador)

| Componente | Pin | GPIO |
| :--- | :--- | :--- |
| **MAX98357A** | BCLK | 22 |
| | LRC | 4 |
| | DIN | 21 |
| **MicroSD** | MISO | 2 |
| | MOSI | 15 |
| | SCK | 14 |
| | CS | 13 |
| **LED** | Ánodo | 12 |

## Instalación y Flasheo

Este proyecto contiene el código para **ambos** microcontroladores en la misma carpeta `src`. Debes seleccionar cuál firmware compilar antes de subirlo.

1.  **Abrir `src/main.cpp`**.
2.  **Seleccionar el Firmware:** Descomenta la línea correspondiente al dispositivo que vas a programar y comenta la otra.

    **Para programar el ESP32 A (Capturador):**
    ```cpp
    #define FIRMWARE_A_CAPTURE
    // #define FIRMWARE_B_PROCESSOR
    ```

    **Para programar el ESP32 B (Procesador):**
    ```cpp
    // #define FIRMWARE_A_CAPTURE
    #define FIRMWARE_B_PROCESSOR
    ```

3.  **Subir:** Conecta el ESP32 correspondiente y dale al botón de "Upload" en PlatformIO.
4.  **Repetir:** Cambia la selección en `src/main.cpp` y repite el proceso para el otro ESP32.

## Uso

1.  **Encender:** Alimenta ambos ESP32. Asegúrate de que estén conectados por UART y GND.
2.  **Configurar (Solo ESP32 B):**
    *   Abre la App móvil y conéctate por Bluetooth a **"Mr. Zorro"**.
    *   La App enviará las credenciales WiFi y el UserID.
    *   El ESP32 B se conectará al WiFi y quedará listo (LED parpadea o indica listo).
3.  **Grabar:**
    *   Mantén presionado el botón en el **ESP32 A**.
    *   El LED del ESP32 A se encenderá. Habla claramente.
    *   El audio se transmite en tiempo real al ESP32 B y se guarda en la SD.
4.  **Procesar:**
    *   Suelta el botón.
    *   El ESP32 A envía la señal de fin.
    *   El ESP32 B cierra el archivo, lo sube al servidor y espera la respuesta.
5.  **Respuesta:**
    *   El ESP32 B descarga la respuesta y la reproduce por el altavoz.

## Dependencias

*   `WiFi`
*   `HTTPClient`
*   `BLEDevice`
*   `ArduinoJson`
*   `SD`
*   `SPI`
*   `Driver/I2S`

## API del Backend

El ESP32 espera un servidor backend con los siguientes endpoints:

*   `POST /audio`: Recibe fragmentos de audio (octet-stream). Encabezados: `X-Chunk-Number`, `X-Last-Chunk`, `X-User-Id`.
*   `GET /get_response/{filename}`: Devuelve el archivo de audio WAV generado.

----

Desarrollado con ♥️ usando PlatformIO y Arduino IDE
