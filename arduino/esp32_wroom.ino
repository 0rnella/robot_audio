#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>

// Wi-Fi credentials
const char* ssid = "Vrouwaanhetij_Gast";
const char* password = "Samensterk!";

// Proxy server IP + port (adjust after running Flask)
const char* server_url = "http://192.168.2.37:5050/upload";  //

// LED optional
#define LED_PIN 22

// Mic I2S pins
#define I2S_SD 32
#define I2S_WS 15
#define I2S_SCK 14

// Recording settings
#define SAMPLE_RATE     16000
#define RECORD_SECONDS  2
#define SAMPLE_SIZE     2  // 16-bit
#define BUFFER_SIZE     (SAMPLE_RATE * RECORD_SECONDS * SAMPLE_SIZE)

uint8_t audio_buffer[BUFFER_SIZE];

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // Connect Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWi-Fi connected!");
  Serial.println(WiFi.localIP());

  // I2S config
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = static_cast<i2s_comm_format_t>(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void loop() {
  Serial.println("🎤 Recording...");
  digitalWrite(LED_PIN, HIGH);

  size_t bytes_read = 0;
  i2s_read(I2S_NUM_0, audio_buffer, BUFFER_SIZE, &bytes_read, portMAX_DELAY);

  // Quick check: print average audio level
  int16_t* samples = (int16_t*)audio_buffer;
  float gain = 10.0; // Try 5.0 – 15.0

  for (int i = 0; i < BUFFER_SIZE / 2; i++) {
    int32_t val = samples[i] * gain;
    if (val > 32767) val = 32767;
    if (val < -32768) val = -32768;
    samples[i] = val;
  }

  int32_t sum = 0;
  for (int i = 0; i < BUFFER_SIZE / 2; i++) {
    sum += abs(samples[i]);
  }
  Serial.print("🔊 Avg signal: ");
  Serial.println(sum / (BUFFER_SIZE / 2));

  digitalWrite(LED_PIN, LOW);
  Serial.println("✅ Done recording. Sending...");

  // Wrap raw audio in WAV header
  WiFiClient client;
  HTTPClient http;
  http.begin(client, server_url);
  http.addHeader("Content-Type", "multipart/form-data; boundary=----WebKitFormBoundary");

  String header = "------WebKitFormBoundary\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String footer = "\r\n------WebKitFormBoundary--\r\n";

  // Allocate full request body buffer
  int bodyLength = header.length() + 44 + BUFFER_SIZE + footer.length();
  uint8_t* full_body = (uint8_t*)malloc(bodyLength);

  // WAV header
  uint8_t wav_header[44];
  writeWAVHeader(wav_header, BUFFER_SIZE, SAMPLE_RATE);

  // Fill buffer
  int pos = 0;
  memcpy(full_body + pos, header.c_str(), header.length()); pos += header.length();
  memcpy(full_body + pos, wav_header, 44); pos += 44;
  memcpy(full_body + pos, audio_buffer, BUFFER_SIZE); pos += BUFFER_SIZE;
  memcpy(full_body + pos, footer.c_str(), footer.length()); pos += footer.length();

  // Send POST request
  int responseCode = http.POST(full_body, bodyLength);
  String reply = http.getString();          // raw JSON from proxy
  Serial.printf("📬 Response code: %d\n", responseCode);
  Serial.println("📄 Raw reply: " + reply);

  DynamicJsonDocument doc(1024);            // 1 kB is enough for {"text":"…"}
  DeserializationError err = deserializeJson(doc, reply);
  if (!err) {
    String transcript = doc["text"].as<String>();
    Serial.println("📝 Transcript: " + transcript);

    /* optional: do something with `transcript` here
       e.g. feed it to ChatGPT, match commands, etc. */
  } else {
    Serial.print("❌ JSON parse error: ");
    Serial.println(err.c_str());
  }

  free(full_body);
  http.end();

  delay(10000);
}

// Writes a basic 16-bit PCM WAV header
void writeWAVHeader(uint8_t* header, int dataSize, int sampleRate) {
  int fileSize = dataSize + 36;
  int byteRate = sampleRate * SAMPLE_SIZE;

  memcpy(header, "RIFF", 4);
  *(uint32_t*)(header + 4) = fileSize;
  memcpy(header + 8, "WAVEfmt ", 8);
  *(uint32_t*)(header + 16) = 16;
  *(uint16_t*)(header + 20) = 1;
  *(uint16_t*)(header + 22) = 1;
  *(uint32_t*)(header + 24) = sampleRate;
  *(uint32_t*)(header + 28) = byteRate;
  *(uint16_t*)(header + 32) = SAMPLE_SIZE;
  *(uint16_t*)(header + 34) = 16;
  memcpy(header + 36, "data", 4);
  *(uint32_t*)(header + 40) = dataSize;
}
