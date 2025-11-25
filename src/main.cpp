/* MR ZORRO - Voice Assistant con Bluetooth BLE

   Hardware:
   INMP441:   SCK=18, WS=19, SD=5
   MAX98357A: BCLK=22, LRC=23, DIN=21
   Botón:     GPIO 12 → GND

   FLUJO: BLE → Recibir UserID → Cerrar BLE → Iniciar I2S/WiFi
*/

#include "driver/i2s.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ========== CONFIGURACIÓN ==========
// WiFi
const char* ssid = "ssid";
const char* password = "password";

// Servidor FastAPI
const char* serverIP = "server_ip_address";
const int serverPort = 8000;

// BLE UUIDs (deben coincidir con Flutter)
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// ⭐ USER ID (se recibirá por Bluetooth)
String userId = "";
bool userIdReceived = false;
bool systemReady = false;  // ⭐ Indica si ya está todo inicializado

// Pines
#define BUTTON_PIN 12
#define MIC_PORT    I2S_NUM_0
#define MIC_BCK     18
#define MIC_WS      19
#define MIC_SD      5
#define SPK_PORT    I2S_NUM_1
#define SPK_BCLK    22
#define SPK_LRC     23
#define SPK_DOUT    21

#define SAMPLE_RATE 16000
#define SAMPLES_PER_CHUNK 2048
#define BUFFER_32BIT_SIZE (SAMPLES_PER_CHUNK * 4)
#define BUFFER_16BIT_SIZE (SAMPLES_PER_CHUNK * 2)

int chunkCounter = 0;
bool isRecording = false;
bool isPlaying = false;
unsigned long totalBytesSent = 0;

// BLE
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

struct WAVHeader {
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
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("📱 Dispositivo BLE conectado");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("📱 Dispositivo BLE desconectado");

    if (!userIdReceived) {
      // Si se desconectó sin enviar userId, reiniciar advertising
      BLEDevice::startAdvertising();
      Serial.println("🔵 Esperando conexión BLE...");
    }
  }
};

class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue().c_str();

    if (value.length() > 0) {
      userId = value;
      userIdReceived = true;

      Serial.println("\n✅ USER ID RECIBIDO:");
      Serial.printf("👤 %s\n\n", userId.c_str());
    }
  }
};

// ========== FUNCIONES SETUP ==========
void setupBLE() {
  Serial.println("🔵 Configurando Bluetooth BLE...");

  // Inicializar BLE
  BLEDevice::init("Mr. Zorro");

  // Crear servidor BLE
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Crear servicio BLE
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Crear característica BLE
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE
  );

  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());

  // Iniciar servicio
  pService->start();

  // Iniciar advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("✅ BLE listo - Esperando conexión...");
  Serial.println("🔍 Nombre del dispositivo: Mr. Zorro");
  Serial.println("\n⏳ Conecta desde la app y envía el User ID...\n");
}

void shutdownBLE() {
  Serial.println("🔵 Cerrando Bluetooth BLE...");

  // Desconectar dispositivos
  if (pServer != NULL) {
    pServer->getAdvertising()->stop();
  }

  delay(500);  // Esperar a que termine cualquier transmisión

  // Liberar toda la memoria BLE
  BLEDevice::deinit(true);

  Serial.println("✅ Bluetooth cerrado - Memoria liberada\n");
}

void setupWiFi() {
  Serial.println("🌐 Conectando a WiFi...");
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi conectado!");
    Serial.printf("📍 IP: %s\n", WiFi.localIP().toString().c_str());
  }
}

void setupSpeaker() {
  Serial.println("🔊 Configurando bocina...");

  i2s_config_t spkConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t spkPins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = SPK_BCLK,
    .ws_io_num = SPK_LRC,
    .data_out_num = SPK_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t err = i2s_driver_install(SPK_PORT, &spkConfig, 0, NULL);
  if (err == ESP_OK) {
    err = i2s_set_pin(SPK_PORT, &spkPins);
    if (err == ESP_OK) {
      i2s_zero_dma_buffer(SPK_PORT);
      Serial.println("✅ Bocina OK");
    } else {
      Serial.printf("❌ Error configurando pines de bocina: %d\n", err);
    }
  } else {
    Serial.printf("❌ Error instalando driver de bocina: %d\n", err);
  }
}

void setupMicrophone() {
  Serial.println("🎤 Configurando micrófono...");

  i2s_config_t micConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t micPins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = MIC_BCK,
    .ws_io_num = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD
  };

  esp_err_t err = i2s_driver_install(MIC_PORT, &micConfig, 0, NULL);
  if (err == ESP_OK) {
    err = i2s_set_pin(MIC_PORT, &micPins);
    if (err == ESP_OK) {
      i2s_zero_dma_buffer(MIC_PORT);
      Serial.println("✅ Micrófono OK");
    } else {
      Serial.printf("❌ Error configurando pines de micrófono: %d\n", err);
    }
  } else {
    Serial.printf("❌ Error instalando driver de micrófono: %d\n", err);
  }
}

void initializeHardware() {
  Serial.println("\n╔═══════════════════════════════════════╗");
  Serial.println("║     INICIALIZANDO HARDWARE...         ║");
  Serial.println("╚═══════════════════════════════════════╝\n");

  setupWiFi();
  delay(100);

  setupSpeaker();
  delay(200);

  setupMicrophone();
  delay(100);

  Serial.println("\n✅ ¡TODO LISTO!");
  Serial.println("🎤 Presiona el botón para hablar\n");

  systemReady = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);

  delay(1000);
  Serial.println("\n╔═══════════════════════════════════════╗");
  Serial.println("║       MR. ZORRO - Voice Assistant     ║");
  Serial.println("╚═══════════════════════════════════════╝\n");

  // ⭐ PASO 1: Solo inicializar BLE
  setupBLE();

  // ⭐ PASO 2: El resto se inicializa en loop() después de recibir userId
}

void startRecording() {
  if (!systemReady) {
    Serial.println("⚠  Sistema no listo");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Sin WiFi");
    return;
  }

  chunkCounter = 0;
  totalBytesSent = 0;
  isRecording = true;
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.printf("🔴 GRABANDO (usuario: %s)...\n", userId.c_str());
}

void streamAudioChunk() {
  if (!isRecording) return;

  static int32_t buffer32[SAMPLES_PER_CHUNK];
  static int16_t buffer16[SAMPLES_PER_CHUNK];
  size_t bytesRead = 0;

  i2s_read(MIC_PORT, buffer32, BUFFER_32BIT_SIZE, &bytesRead, portMAX_DELAY);

  if (bytesRead > 0) {
    int samples = bytesRead / 4;

    for (int i = 0; i < samples; i++) {
      int32_t s32 = buffer32[i] >> 8;
      int32_t s16 = (s32 >> 8) * 3;

      if (s16 > 32767) s16 = 32767;
      if (s16 < -32768) s16 = -32768;

      buffer16[i] = (int16_t)s16;
    }

    chunkCounter++;
    size_t toSend = samples * 2;
    totalBytesSent += toSend;

    HTTPClient http;
    http.begin(String("http://") + serverIP + ":" + serverPort + "/audio");
    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("X-Chunk-Number", String(chunkCounter));
    http.addHeader("X-Last-Chunk", "false");
    http.addHeader("X-User-Id", userId);
    http.setTimeout(5000);
    http.POST((uint8_t*)buffer16, toSend);
    http.end();

    if (chunkCounter % 5 == 0) {
      float dur = (float)totalBytesSent / (SAMPLE_RATE * 2);
      Serial.printf("📦 %d chunks (%.1fs)\n", chunkCounter, dur);
    }
  }
}

void stopRecording() {
  if (!isRecording) return;

  isRecording = false;
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println("\n✅ Grabación completa");
  Serial.println("⏳ Esperando respuesta de IA...");

  HTTPClient http;
  http.begin(String("http://") + serverIP + ":" + serverPort + "/audio");
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("X-Chunk-Number", String(chunkCounter + 1));
  http.addHeader("X-Last-Chunk", "true");
  http.addHeader("X-User-Id", userId);
  http.setTimeout(120000);

  uint8_t dummy = 0;
  int code = http.POST(&dummy, 1);

  if (code == 200) {
    String resp = http.getString();
    Serial.println("📥 Respuesta:");
    Serial.println(resp);

    int start = resp.indexOf("\"filename\":\"") + 12;
    int end = resp.indexOf("\"", start);

    if (start > 11 && end > start) {
      String file = resp.substring(start, end);
      Serial.printf("🎵 Reproduciendo: %s\n", file.c_str());

      delay(500);
      playAudio(file);
    }
  } else {
    Serial.printf("❌ Error HTTP: %d\n", code);
  }

  http.end();
}

void playAudio(String filename) {
  if (isPlaying) return;
  isPlaying = true;

  Serial.println("🔊 Descargando...");

  HTTPClient http;
  http.begin(String("http://") + serverIP + ":" + serverPort + "/get_response/" + filename);
  int code = http.GET();

  if (code != 200) {
    Serial.printf("❌ Error: %d\n", code);
    http.end();
    isPlaying = false;
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  int size = http.getSize();

  WAVHeader hdr;
  stream->readBytes((uint8_t*)&hdr, sizeof(WAVHeader));

  Serial.printf("📊 %dHz, %d ch, %d bits\n", hdr.sampleRate, hdr.numChannels, hdr.bitsPerSample);

  if (hdr.audioFormat != 1 || hdr.bitsPerSample != 16) {
    Serial.println("❌ Formato inválido");
    http.end();
    isPlaying = false;
    return;
  }

  Serial.println("▶  REPRODUCIENDO...\n");

  i2s_zero_dma_buffer(SPK_PORT);
  delay(100);

  uint8_t* buf = (uint8_t*)malloc(4096);
  if (!buf) {
    http.end();
    isPlaying = false;
    return;
  }

  int remaining = size - sizeof(WAVHeader);
  int total = 0;
  size_t written;
  unsigned long lastPrint = millis();

  while (http.connected() && remaining > 0) {
    size_t avail = stream->available();

    if (avail) {
      int toRead = min((int)avail, min(4096, remaining));
      int read = stream->readBytes(buf, toRead);

      if (read > 0) {
        if (hdr.numChannels == 1) {
          uint8_t* st = (uint8_t*)malloc(read * 2);
          if (st) {
            int16_t* mono = (int16_t*)buf;
            int16_t* stereo = (int16_t*)st;
            int samples = read / 2;

            for (int i = 0; i < samples; i++) {
              int32_t amp = mono[i] * 3;
              if (amp > 32767) amp = 32767;
              if (amp < -32768) amp = -32768;

              int16_t s = (int16_t)amp;
              stereo[i * 2] = s;
              stereo[i * 2 + 1] = s;
            }

            i2s_write(SPK_PORT, st, read * 2, &written, portMAX_DELAY);
            free(st);
          }
        } else {
          int16_t* samples = (int16_t*)buf;
          int count = read / 2;

          for (int i = 0; i < count; i++) {
            int32_t amp = samples[i] * 3;
            if (amp > 32767) amp = 32767;
            if (amp < -32768) amp = -32768;
            samples[i] = (int16_t)amp;
          }

          i2s_write(SPK_PORT, buf, read, &written, portMAX_DELAY);
        }

        total += read;
        remaining -= read;

        if (millis() - lastPrint > 1000) {
          float pct = (float)total / (size - sizeof(WAVHeader)) * 100;
          Serial.printf("🎵 %.0f%%\n", pct);
          lastPrint = millis();
        }
      }
    }
    yield();
  }

  free(buf);
  http.end();

  delay(300);
  i2s_zero_dma_buffer(SPK_PORT);

  Serial.println("\n✅ Reproducción completa\n");
  isPlaying = false;
}

void loop() {
  // ⭐ Si aún no hemos recibido el userId, solo esperamos
  if (!userIdReceived) {
    delay(100);
    return;
  }

  // ⭐ Si recibimos userId pero no hemos inicializado hardware, hacerlo ahora
  if (userIdReceived && !systemReady) {
    shutdownBLE();  // Cerrar BLE primero
    delay(1000);    // Esperar a que libere memoria
    initializeHardware();  // Inicializar todo lo demás
    return;
  }

  // ⭐ Sistema listo, funcionalidad normal
  static bool was = false;
  int btn = digitalRead(BUTTON_PIN);

  if (btn == LOW) {
    if (!was && !isPlaying) {
      startRecording();
      was = true;
    }
    streamAudioChunk();
  } else {
    if (was) {
      stopRecording();
      was = false;
    }
  }

  if (!isRecording && !isPlaying) delay(10);
}