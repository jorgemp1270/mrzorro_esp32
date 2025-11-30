/* LILYGO T-SIM7000G - Procesador de Audio
   Hardware:
   MAX98357A: BCLK=22, LRC=4, DIN=21
   MicroSD:   MISO=2, MOSI=15, SCK=14, CS=13
   UART2:     TX=26, RX=27 (recibe de NodeMCU)
   LED:       GPIO 12

   Flujo:
   1. BLE → Recibe configuración (WiFi, User ID, Server)
   2. UART → Recibe audio del NodeMCU
   3. SD → Guarda audio
   4. HTTP → Envía al servidor
   5. I2S → Reproduce respuesta
*/

#include "driver/i2s.h"
#include "driver/uart.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "SD.h"
#include "SPI.h"
#include <ArduinoJson.h>

// ========== CONFIGURACIÓN ==========
const int serverPort = 8000;

// BLE UUIDs
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// ========== VARIABLES DE CONFIGURACIÓN ==========
String userId = "";
String wifiSSID = "";
String wifiPassword = "";
String serverIP = "";
bool userIdReceived = false;
bool systemReady = false;
bool sdCardReady = false;

// ========== PINES LILYGO T-SIM7000G ==========
// 🔊 BOCINA MAX98357A (I2S)
#define SPK_BCLK 22
#define SPK_LRC 4
#define SPK_DOUT 21

// 💾 MICRO SD (Pines personalizados)
#define SD_MISO 2
#define SD_MOSI 15
#define SD_SCK 14
#define SD_CS 13

// 📡 UART para recibir de NodeMCU
#define UART_TX 26 // TX del LilyGo (no usado)
#define UART_RX 27 // RX del LilyGo → conectar a TX del NodeMCU
#define UART_NUM UART_NUM_2
#define UART_BAUD 921600

// 💡 LED
#define LED_PIN 12

#define SPK_PORT I2S_NUM_0
#define SAMPLE_RATE 16000

// ========== VARIABLES DE AUDIO ==========
bool isReceiving = false;
bool isPlaying = false;
File audioFile;
const char *recordingPath = "/recording.pcm";
const char *responsePath = "/response.wav";

// ========== BLE ==========
BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;

struct WAVHeader
{
    char riff[4];
    uint32_t fileSize;
    char wave[4];
    char fmt[4];
    uint32_t fmtSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char data[4];
    uint32_t dataSize;
};

// ========== CALLBACKS BLE ==========
class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *pServer)
    {
        deviceConnected = true;
        Serial.println("📱 Dispositivo BLE conectado");
    }

    void onDisconnect(BLEServer *pServer)
    {
        deviceConnected = false;
        Serial.println("📱 Dispositivo BLE desconectado");

        if (!userIdReceived)
        {
            BLEDevice::startAdvertising();
            Serial.println("🔵 Esperando conexión BLE...");
        }
    }
};

class MyCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pCharacteristic)
    {
        String value = pCharacteristic->getValue().c_str();

        if (value.length() > 0)
        {
            Serial.println("\n📦 Datos recibidos desde la app:");
            Serial.println(value);

            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, value);

            if (error)
            {
                Serial.print("❌ Error parseando JSON: ");
                Serial.println(error.c_str());
                return;
            }

            if (doc.containsKey("userid"))
            {
                userId = doc["userid"].as<String>();
                Serial.printf("👤 User ID: %s\n", userId.c_str());
            }

            if (doc.containsKey("ssid"))
            {
                wifiSSID = doc["ssid"].as<String>();
                Serial.printf("📡 WiFi SSID: %s\n", wifiSSID.c_str());
            }

            if (doc.containsKey("wifi_password"))
            {
                wifiPassword = doc["wifi_password"].as<String>();
                Serial.printf("🔑 WiFi Password: %s\n", wifiPassword.c_str());
            }

            if (doc.containsKey("api_host"))
            {
                serverIP = doc["api_host"].as<String>();
                Serial.printf("🌐 API Host: %s\n", serverIP.c_str());
            }

            if (userId.length() > 0 && wifiSSID.length() > 0 && wifiPassword.length() > 0)
            {
                userIdReceived = true;
                Serial.println("\n✅ TODOS LOS DATOS RECIBIDOS\n");
            }
        }
    }
};

// ========== SETUP BLE ==========
void setupBLE()
{
    Serial.println("🔵 Configurando Bluetooth BLE...");

    BLEDevice::init("Mr. Zorro");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

    pCharacteristic->setCallbacks(new MyCallbacks());
    pCharacteristic->addDescriptor(new BLE2902());
    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("✅ BLE listo - Esperando conexión...");
    Serial.println("🔍 Nombre: Mr. Zorro\n");
}

void shutdownBLE()
{
    Serial.println("🔵 Cerrando Bluetooth...");
    if (pServer != NULL)
    {
        pServer->getAdvertising()->stop();
    }
    delay(500);
    BLEDevice::deinit(true);
    Serial.println("✅ Bluetooth cerrado\n");
}

// ========== SETUP UART ==========
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

    uart_driver_install(UART_NUM, 16384, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, UART_TX, UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    Serial.println("✅ UART2 listo");
}

// ========== SETUP SD ==========
void setupSDCard()
{
    Serial.println("💾 Configurando SD...");

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    if (!SD.begin(SD_CS))
    {
        Serial.println("❌ Error montando SD");
        sdCardReady = false;
        return;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE)
    {
        Serial.println("❌ No hay tarjeta SD");
        sdCardReady = false;
        return;
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("📊 SD: %lluMB\n", cardSize);

    sdCardReady = true;
    Serial.println("✅ SD lista\n");
}

// ========== SETUP WIFI ==========
void setupWiFi()
{
    Serial.println("🌐 Conectando WiFi...");
    Serial.printf("📡 SSID: %s\n", wifiSSID.c_str());

    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\n✅ WiFi conectado!");
        Serial.printf("📍 IP: %s\n", WiFi.localIP().toString().c_str());
    }
    else
    {
        Serial.println("\n❌ WiFi falló");
    }
}

// ========== SETUP BOCINA ==========
void setupSpeaker()
{
    Serial.println("🔊 Configurando bocina...");

    i2s_config_t spkConfig = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0};

    i2s_pin_config_t spkPins = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = SPK_BCLK,
        .ws_io_num = SPK_LRC,
        .data_out_num = SPK_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE};

    esp_err_t err = i2s_driver_install(SPK_PORT, &spkConfig, 0, NULL);
    if (err == ESP_OK)
    {
        err = i2s_set_pin(SPK_PORT, &spkPins);
        if (err == ESP_OK)
        {
            i2s_zero_dma_buffer(SPK_PORT);
            Serial.println("✅ Bocina OK");
        }
    }
}

void initializeHardware()
{
    Serial.println("\n╔═════════════════════════════════╗");
    Serial.println("║   INICIALIZANDO HARDWARE...     ║");
    Serial.println("╚═════════════════════════════════╝\n");

    setupSDCard();
    delay(100);

    setupWiFi();
    delay(100);

    setupSpeaker();
    delay(200);

    setupUART();
    delay(100);

    Serial.println("\n✅ ¡SISTEMA LISTO!\n");
    systemReady = true;
}

// ========== REPRODUCIR AUDIO ==========
void playAudioFromSD()
{
    if (isPlaying)
        return;
    isPlaying = true;

    Serial.println("🔊 Reproduciendo...");

    File file = SD.open(responsePath, FILE_READ);
    if (!file)
    {
        Serial.println("❌ Error abriendo respuesta");
        isPlaying = false;
        return;
    }

    WAVHeader hdr;
    if (file.read((uint8_t *)&hdr, sizeof(WAVHeader)) != sizeof(WAVHeader))
    {
        Serial.println("❌ Error leyendo WAV header");
        file.close();
        isPlaying = false;
        return;
    }

    Serial.printf("📊 %dHz, %dch, %dbits\n", hdr.sampleRate, hdr.numChannels, hdr.bitsPerSample);

    i2s_zero_dma_buffer(SPK_PORT);
    delay(100);

    uint8_t *buf = (uint8_t *)malloc(4096);
    if (!buf)
    {
        file.close();
        isPlaying = false;
        return;
    }

    size_t written;
    while (file.available())
    {
        int read = file.read(buf, 4096);
        if (read > 0)
        {
            if (hdr.numChannels == 1)
            {
                // Convertir mono a estéreo con amplificación
                int16_t *mono = (int16_t *)buf;
                int samples = read / 2;
                int16_t *stereo = (int16_t *)malloc(samples * 4);

                if (stereo)
                {
                    for (int i = 0; i < samples; i++)
                    {
                        int32_t amp = mono[i] * 3;
                        if (amp > 32767)
                            amp = 32767;
                        if (amp < -32768)
                            amp = -32768;

                        int16_t s = (int16_t)amp;
                        stereo[i * 2] = s;
                        stereo[i * 2 + 1] = s;
                    }
                    i2s_write(SPK_PORT, stereo, samples * 4, &written, portMAX_DELAY);
                    free(stereo);
                }
            }
            else
            {
                // Estéreo con amplificación
                int16_t *samples = (int16_t *)buf;
                int count = read / 2;
                for (int i = 0; i < count; i++)
                {
                    int32_t amp = samples[i] * 3;
                    if (amp > 32767)
                        amp = 32767;
                    if (amp < -32768)
                        amp = -32768;
                    samples[i] = (int16_t)amp;
                }
                i2s_write(SPK_PORT, buf, read, &written, portMAX_DELAY);
            }
        }
    }

    free(buf);
    file.close();
    delay(300);
    i2s_zero_dma_buffer(SPK_PORT);

    Serial.println("✅ Reproducción completa\n");
    isPlaying = false;
}

// ========== ENVIAR AL SERVIDOR ==========
void sendAudioToServer()
{
    File file = SD.open(recordingPath, FILE_READ);
    if (!file)
    {
        Serial.println("❌ Error abriendo grabación");
        return;
    }

    size_t fileSize = file.size();
    Serial.printf("📦 Enviando %d bytes al servidor...\n", fileSize);

    size_t bytesSent = 0;
    int chunkNum = 0;
    const size_t CHUNK_SIZE = 4096;
    uint8_t *buffer = (uint8_t *)malloc(CHUNK_SIZE);

    if (!buffer)
    {
        Serial.println("❌ Error malloc");
        file.close();
        return;
    }

    HTTPClient http;

    while (file.available())
    {
        int bytesRead = file.read(buffer, CHUNK_SIZE);
        chunkNum++;
        bool isLast = (bytesSent + bytesRead) >= fileSize;

        http.begin(String("http://") + serverIP + ":" + serverPort + "/audio");
        http.addHeader("Content-Type", "application/octet-stream");
        http.addHeader("X-Chunk-Number", String(chunkNum));
        http.addHeader("X-Last-Chunk", isLast ? "true" : "false");
        http.addHeader("X-User-Id", userId);
        http.setTimeout(isLast ? 60000 : 5000);

        int code = http.POST(buffer, bytesRead);

        if (code == 200)
        {
            Serial.printf("✅ Chunk %d enviado\n", chunkNum);

            if (isLast)
            {
                Serial.println("📥 Recibiendo respuesta...");
                SD.remove(responsePath);
                File respFile = SD.open(responsePath, FILE_WRITE);
                if (respFile)
                {
                    http.writeToStream(&respFile);
                    respFile.close();
                    Serial.println("✅ Respuesta guardada");
                    playAudioFromSD();
                }
            }
        }
        else
        {
            Serial.printf("❌ HTTP Error: %d\n", code);
        }
        http.end();

        bytesSent += bytesRead;
    }

    free(buffer);
    file.close();
}

// ========== RECIBIR AUDIO POR UART ==========
void receiveAudioFromUART()
{
    uint8_t buffer[256];
    int len = uart_read_bytes(UART_NUM, buffer, sizeof(buffer), 10 / portTICK_PERIOD_MS);

    if (len > 0)
    {
        // Buscar comandos
        String cmd = String((char *)buffer);

        if (cmd.indexOf("START") >= 0 && !isReceiving)
        {
            Serial.println("\n🔴 RECIBIENDO AUDIO...");
            digitalWrite(LED_PIN, HIGH);
            SD.remove(recordingPath);
            audioFile = SD.open(recordingPath, FILE_WRITE);
            if (audioFile)
            {
                isReceiving = true;
            }
            else
            {
                Serial.println("❌ Error creando archivo");
            }
        }
        else if (cmd.indexOf("STOP") >= 0 && isReceiving)
        {
            Serial.println("✅ Recepción completa");
            digitalWrite(LED_PIN, LOW);
            isReceiving = false;
            if (audioFile)
            {
                audioFile.close();
            }
            Serial.println("⏳ Enviando a servidor...");
            sendAudioToServer();
        }
        else if (isReceiving && audioFile)
        {
            // Escribir datos de audio
            audioFile.write(buffer, len);
        }
    }
}

// ========== SETUP ==========
void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);

    delay(1000);

    Serial.println("\n╔═══════════════════════════════════════╗");
    Serial.println("║   LILYGO T-SIM7000G - Audio Processor ║");
    Serial.println("║   UART → SD → Server → Speaker        ║");
    Serial.println("╚═══════════════════════════════════════╝\n");

    Serial.println("📋 Esperando configuración BLE...\n");
    setupBLE();
}

// ========== LOOP ==========
void loop()
{
    // Esperar configuración BLE
    if (!userIdReceived)
    {
        delay(100);
        return;
    }

    // Inicializar hardware después de recibir config
    if (userIdReceived && !systemReady)
    {
        shutdownBLE();
        delay(1000);
        initializeHardware();
        return;
    }

    // Procesar audio recibido por UART
    if (systemReady && !isPlaying)
    {
        receiveAudioFromUART();
    }

    delay(5);
}