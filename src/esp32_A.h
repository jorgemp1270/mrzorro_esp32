/* NODEMCU-32S - Capturador de Audio
   Hardware:
   INMP441:   BCK=26, WS=25, SD=33
   Botón:     GPIO 27 → GND
   LED:       GPIO 2
   UART2:     TX=17, RX=16 (comunicación con LilyGo)

   Protocolo UART:
   - Comando START: "START\n"
   - Comando STOP: "STOP\n"
   - Datos: chunks de 4096 bytes (2048 samples x 2 bytes)
*/

#include "driver/i2s.h"
#include "driver/uart.h"

// ========== PINES ==========
#define MIC_BCK 26
#define MIC_WS 25
#define MIC_SD 33
#define BUTTON_PIN 27
#define LED_PIN 2

// UART para comunicación con LilyGo
#define UART_TX 17
#define UART_RX 16
#define UART_NUM UART_NUM_2
#define UART_BAUD 921600 // Alta velocidad para audio

// ========== CONFIGURACIÓN I2S ==========
#define MIC_PORT I2S_NUM_0
#define SAMPLE_RATE 16000
#define SAMPLES_PER_CHUNK 2048
#define BUFFER_32BIT_SIZE (SAMPLES_PER_CHUNK * 4)
#define BUFFER_16BIT_SIZE (SAMPLES_PER_CHUNK * 2)

// ========== VARIABLES GLOBALES ==========
bool isRecording = false;
int chunkCounter = 0;

void setupUART()
{
    Serial.println("📡 Configurando UART2...");

    uart_config_t uart_config = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_APB,
    };

    uart_driver_install(UART_NUM, 8192, 8192, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, UART_TX, UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    Serial.println("✅ UART2 listo");
}

void setupMicrophone()
{
    Serial.println("🎤 Configurando micrófono INMP441...");

    i2s_config_t micConfig = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0};

    i2s_pin_config_t micPins = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = MIC_BCK,
        .ws_io_num = MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_SD};

    esp_err_t err = i2s_driver_install(MIC_PORT, &micConfig, 0, NULL);
    if (err == ESP_OK)
    {
        err = i2s_set_pin(MIC_PORT, &micPins);
        if (err == ESP_OK)
        {
            i2s_zero_dma_buffer(MIC_PORT);
            Serial.println("✅ Micrófono OK");
        }
        else
        {
            Serial.printf("❌ Error pines micrófono: %d\n", err);
        }
    }
    else
    {
        Serial.printf("❌ Error driver micrófono: %d\n", err);
    }
}

void sendUARTCommand(const char *cmd)
{
    uart_write_bytes(UART_NUM, cmd, strlen(cmd));
    Serial.printf("📤 Comando enviado: %s", cmd);
}

void startRecording()
{
    isRecording = true;
    chunkCounter = 0;
    digitalWrite(LED_PIN, HIGH);

    Serial.println("\n🔴 INICIANDO GRABACIÓN...");
    sendUARTCommand("START\n");
    delay(50); // Dar tiempo al receptor para prepararse
}

void captureAndSendAudio()
{
    if (!isRecording)
        return;

    static int32_t buffer32[SAMPLES_PER_CHUNK];
    static int16_t buffer16[SAMPLES_PER_CHUNK];
    size_t bytesRead = 0;

    // Leer audio del micrófono
    i2s_read(MIC_PORT, buffer32, BUFFER_32BIT_SIZE, &bytesRead, portMAX_DELAY);

    if (bytesRead > 0)
    {
        int samples = bytesRead / 4;

        // Convertir de 32-bit a 16-bit con ganancia
        for (int i = 0; i < samples; i++)
        {
            int32_t s32 = buffer32[i] >> 14; // Shift + ganancia x4

            if (s32 > 32767)
                s32 = 32767;
            if (s32 < -32768)
                s32 = -32768;

            buffer16[i] = (int16_t)s32;
        }

        // Enviar por UART
        int bytesSent = uart_write_bytes(UART_NUM, (const char *)buffer16, samples * 2);

        if (bytesSent > 0)
        {
            chunkCounter++;

            // Debug cada 10 chunks
            if (chunkCounter % 10 == 0)
            {
                Serial.printf("🎤 Chunk %d enviado (%d bytes) - Sample[0]: %d\n",
                              chunkCounter, bytesSent, buffer16[0]);
            }
        }
        else
        {
            Serial.println("⚠  Error enviando datos por UART");
        }
    }
}

void stopRecording()
{
    if (!isRecording)
        return;

    isRecording = false;
    digitalWrite(LED_PIN, LOW);

    Serial.printf("\n✅ Grabación completa - %d chunks enviados\n", chunkCounter);
    sendUARTCommand("STOP\n");

    delay(100); // Asegurar que se envíe todo
}

void setup()
{
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);

    delay(1000);

    Serial.println("\n╔═══════════════════════════════════════╗");
    Serial.println("║   NODEMCU-32S - Audio Capture         ║");
    Serial.println("║   INMP441 → UART → LilyGo             ║");
    Serial.println("╚═══════════════════════════════════════╝\n");

    setupUART();
    delay(100);

    setupMicrophone();
    delay(100);

    Serial.println("\n✅ Sistema listo");
    Serial.println("🎤 Presiona el botón para grabar\n");
}

void loop()
{
    static bool wasPressed = false;
    int buttonState = digitalRead(BUTTON_PIN);

    // Botón presionado (activo en LOW)
    if (buttonState == LOW)
    {
        if (!wasPressed)
        {
            startRecording();
            wasPressed = true;
        }
        captureAndSendAudio();
    }
    // Botón liberado
    else
    {
        if (wasPressed)
        {
            stopRecording();
            wasPressed = false;
        }
    }

    if (!isRecording)
    {
        delay(10);
    }
}