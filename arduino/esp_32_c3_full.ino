#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>

// Wi-Fi credentials
const char* ssid = "Pixie_n_Friends_Upstairs";
const char* password = "TastyTr3atz";

// Proxy server IP + port (adjust to your server)
const char* server_url = "http://192.168.2.50:5050/upload";

// Button pin
#define BUTTON_PIN 3
#define LED_PIN 8  // Built-in LED on many ESP32-C3 boards

// Mic I2S pins (using tested working pins)
#define I2S_SD 0   // Serial Data
#define I2S_WS 2   // Word Select  
#define I2S_SCK 1  // Serial Clock

// Recording settings
#define SAMPLE_RATE     16000
#define RECORD_SECONDS  3      // 3 seconds should be good for questions
#define SAMPLE_SIZE     2      // 16-bit
#define BUFFER_SIZE     (SAMPLE_RATE * RECORD_SECONDS * SAMPLE_SIZE)

uint8_t audio_buffer[BUFFER_SIZE];

// Button state tracking
bool lastButtonState = HIGH;
bool recording = false;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  // Setup pins
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("🤖 AI Robot Starting Up!");

  // Connect Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("📶 Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); 
    Serial.print(".");
  }
  Serial.println("\n✅ Wi-Fi connected!");
  Serial.print("🌐 IP Address: ");
  Serial.println(WiFi.localIP());

  // I2S config for ESP32-C3
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
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

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("❌ Failed installing I2S driver: %d\n", err);
    while (true);
  }
  
  err = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("❌ Failed setting I2S pins: %d\n", err);
    while (true);
  }

  Serial.println("🎤 Microphone ready!");
  Serial.println("🔘 Press button to record and ask a question!");
}

void loop() {
  // Read button state
  bool currentButtonState = digitalRead(BUTTON_PIN);
  
  // Button pressed (falling edge)
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    delay(50); // Debounce
    if (!recording) {
      startRecording();
    }
  }
  
  // Button released (rising edge) 
  if (lastButtonState == LOW && currentButtonState == HIGH) {
    delay(50); // Debounce
    if (recording) {
      stopRecordingAndProcess();
    }
  }
  
  lastButtonState = currentButtonState;
  delay(10);
}

void startRecording() {
  recording = true;
  digitalWrite(LED_PIN, HIGH);
  Serial.println("🎤 Recording started... Release button when done!");
  
  // Clear the buffer
  memset(audio_buffer, 0, BUFFER_SIZE);
}

void stopRecordingAndProcess() {
  if (!recording) return;
  
  recording = false;
  Serial.println("🎤 Recording...");

  size_t bytes_read = 0;
  size_t total_bytes_read = 0;
  
  // Record audio in chunks
  while (total_bytes_read < BUFFER_SIZE) {
    size_t chunk_size = min(BUFFER_SIZE - total_bytes_read, (size_t)2048);
    i2s_read(I2S_NUM_0, audio_buffer + total_bytes_read, chunk_size, &bytes_read, 1000);
    total_bytes_read += bytes_read;
  }

  // Process audio with gain
  int16_t* samples = (int16_t*)audio_buffer;
  float gain = 8.0; // Adjust if too quiet/loud

  for (int i = 0; i < BUFFER_SIZE / 2; i++) {
    int32_t val = samples[i] * gain;
    if (val > 32767) val = 32767;
    if (val < -32768) val = -32768;
    samples[i] = val;
  }

  // Calculate average signal level
  int32_t sum = 0;
  for (int i = 0; i < BUFFER_SIZE / 2; i++) {
    sum += abs(samples[i]);
  }
  int avg_signal = sum / (BUFFER_SIZE / 2);
  Serial.printf("🔊 Avg signal level: %d\n", avg_signal);

  digitalWrite(LED_PIN, LOW);
  Serial.println("✅ Recording complete! Sending to AI...");

  // Send to server
  sendAudioToServer();
}

void sendAudioToServer() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Wi-Fi not connected!");
    return;
  }

  WiFiClient client;
  HTTPClient http;
  http.begin(client, server_url);
  http.addHeader("Content-Type", "multipart/form-data; boundary=----WebKitFormBoundary");
  http.setTimeout(30000); // 30 second timeout

  String header = "------WebKitFormBoundary\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String footer = "\r\n------WebKitFormBoundary--\r\n";

  // Create WAV file
  int bodyLength = header.length() + 44 + BUFFER_SIZE + footer.length();
  uint8_t* full_body = (uint8_t*)malloc(bodyLength);
  
  if (!full_body) {
    Serial.println("❌ Failed to allocate memory!");
    return;
  }

  // WAV header
  uint8_t wav_header[44];
  writeWAVHeader(wav_header, BUFFER_SIZE, SAMPLE_RATE);

  // Assemble the request body
  int pos = 0;
  memcpy(full_body + pos, header.c_str(), header.length()); 
  pos += header.length();
  memcpy(full_body + pos, wav_header, 44); 
  pos += 44;
  memcpy(full_body + pos, audio_buffer, BUFFER_SIZE); 
  pos += BUFFER_SIZE;
  memcpy(full_body + pos, footer.c_str(), footer.length());

  // Send request
  Serial.println("📤 Sending audio to AI server...");
  int responseCode = http.POST(full_body, bodyLength);
  String reply = http.getString();
  
  Serial.printf("📬 Response code: %d\n", responseCode);
  
  if (responseCode == 200) {
    // Parse JSON response
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, reply);
    
    if (!err && doc.containsKey("text")) {
      String transcript = doc["text"].as<String>();
      Serial.println("🤖 AI heard: \"" + transcript + "\"");
      
      // Flash LED to indicate successful transcription
      for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
      }
      
    } else {
      Serial.println("❌ Failed to parse AI response");
      Serial.println("Raw response: " + reply);
    }
  } else {
    Serial.println("❌ Failed to send audio to server");
    Serial.println("Response: " + reply);
  }

  free(full_body);
  http.end();
  
  Serial.println("🔘 Ready for next question! Press button to record.");
}

// Create WAV header for the audio data
void writeWAVHeader(uint8_t* header, int dataSize, int sampleRate) {
  int fileSize = dataSize + 36;
  int byteRate = sampleRate * SAMPLE_SIZE;

  memcpy(header, "RIFF", 4);
  *(uint32_t*)(header + 4) = fileSize;
  memcpy(header + 8, "WAVEfmt ", 8);
  *(uint32_t*)(header + 16) = 16;           // Subchunk1Size
  *(uint16_t*)(header + 20) = 1;            // AudioFormat (PCM)
  *(uint16_t*)(header + 22) = 1;            // NumChannels
  *(uint32_t*)(header + 24) = sampleRate;   // SampleRate
  *(uint32_t*)(header + 28) = byteRate;     // ByteRate
  *(uint16_t*)(header + 32) = SAMPLE_SIZE;  // BlockAlign
  *(uint16_t*)(header + 34) = 16;           // BitsPerSample
  memcpy(header + 36, "data", 4);
  *(uint32_t*)(header + 40) = dataSize;     // Subchunk2Size
}