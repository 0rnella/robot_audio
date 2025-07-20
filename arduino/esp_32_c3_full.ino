#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>
#include "AudioFileSourceHTTPStream.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// Wi-Fi credentials
const char* ssid = "pixie";
const char* password = "ornellaf";

// Proxy server IP + port (adjust to your server)
const char* server_url = "http://192.168.2.24:5050/upload";

// Button pin
#define BUTTON_PIN 3
#define LED_PIN 8  // Built-in LED on many ESP32-C3 boards

// Simple wiring setup
#define AUDIO_OUT_PIN 9  // DAC output to PAM8403 L+ input

// Mic I2S pins (using tested working pins)
#define I2S_SD 0   // Serial Data
#define I2S_WS 2   // Word Select  
#define I2S_SCK 1  // Serial Clock

// Recording settings
#define SAMPLE_RATE     16000
#define RECORD_SECONDS  2      // Reduce to 2 seconds to save memory
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
  pinMode(AUDIO_OUT_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("🤖 AI Robot Starting Up!");

  // Connect Wi-Fi
  Serial.println("📶 Starting Wi-Fi connection...");
  
  // More aggressive Wi-Fi setup for ESP32-C3
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  
  // Scan for available networks first
  Serial.println("🔍 Scanning for networks...");
  int n = WiFi.scanNetworks();
  Serial.printf("Found %d networks:\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("%d: %s (Signal: %d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
  }
  Serial.println("🔍 Scan complete\n");
  
  Serial.print("SSID: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  // Try additional ESP32-specific settings
  WiFi.setHostname("ESP32-Robot");
  delay(1000);
  
  Serial.print("📶 Connecting to Wi-Fi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {  // 10 second timeout
    delay(500); 
    Serial.print(".");
    attempts++;
    
    // Print status every few attempts
    if (attempts % 4 == 0) {
      Serial.print(" [Status: ");
      Serial.print(WiFi.status());
      Serial.print("]");
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Wi-Fi connected!");
    Serial.print("🌐 IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ Wi-Fi connection failed!");
    Serial.print("Final status: ");
    Serial.println(WiFi.status());
    Serial.println("Continuing anyway...");
  }

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
  
  // Button pressed (falling edge) - start recording
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    delay(50); // Debounce
    startRecording();
  }
  
  // Button released (rising edge) - stop recording
  if (lastButtonState == LOW && currentButtonState == HIGH) {
    delay(50); // Debounce
    if (recording) {
      stopRecordingAndProcess();
    }
  }
  
  // While recording, keep collecting audio data
  if (recording) {
    recordAudioData();
  }
  
  lastButtonState = currentButtonState;
  delay(10);
}

// Recording state
int recording_position = 0;

void startRecording() {
  recording = true;
  recording_position = 0;
  digitalWrite(LED_PIN, HIGH);
  Serial.println("🎤 Recording... Keep button pressed, release when done!");
  
  // Clear the buffer
  memset(audio_buffer, 0, BUFFER_SIZE);
}

void recordAudioData() {
  if (!recording) return;
  
  // Read a small chunk of audio data
  const int chunk_size = 512;
  size_t bytes_read = 0;
  
  if (recording_position < BUFFER_SIZE) {
    int remaining_space = BUFFER_SIZE - recording_position;
    int read_size = min(chunk_size, remaining_space);
    
    i2s_read(I2S_NUM_0, audio_buffer + recording_position, read_size, &bytes_read, 10);
    recording_position += bytes_read;
    
    // If buffer is full, stop recording
    if (recording_position >= BUFFER_SIZE) {
      Serial.println("📏 Buffer full! Stopping recording...");
      stopRecordingAndProcess();
    }
  }
}

void stopRecordingAndProcess() {
  if (!recording) return;
  
  recording = false;
  digitalWrite(LED_PIN, LOW);
  
  Serial.printf("🎤 Recording complete! Recorded %d bytes\n", recording_position);

  // Process audio with gain (only process the recorded portion)
  int16_t* samples = (int16_t*)audio_buffer;
  int sample_count = recording_position / 2;  // 16-bit samples
  float gain = 8.0; // Adjust if too quiet/loud

  for (int i = 0; i < sample_count; i++) {
    int32_t val = samples[i] * gain;
    if (val > 32767) val = 32767;
    if (val < -32768) val = -32768;
    samples[i] = val;
  }

  // Calculate average signal level
  int32_t sum = 0;
  for (int i = 0; i < sample_count; i++) {
    sum += abs(samples[i]);
  }
  int avg_signal = (sample_count > 0) ? sum / sample_count : 0;
  Serial.printf("🔊 Avg signal level: %d\n", avg_signal);

  Serial.println("✅ Processing complete! Sending to AI...");

  // Send to server (use actual recorded size, not full buffer)
  sendAudioToServer(recording_position);
}

void sendAudioToServer(int actual_data_size) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Wi-Fi not connected!");
    return;
  }

  // Check available heap memory
  Serial.printf("🧠 Free heap: %d bytes\n", ESP.getFreeHeap());

  WiFiClient client;
  HTTPClient http;
  http.begin(client, server_url);
  http.addHeader("Content-Type", "multipart/form-data; boundary=----WebKitFormBoundary");
  http.setTimeout(60000); // Increase to 60 seconds for TTS processing

  String header = "------WebKitFormBoundary\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String footer = "\r\n------WebKitFormBoundary--\r\n";

  // Calculate required size (use actual recorded data size)
  int bodyLength = header.length() + 44 + actual_data_size + footer.length();
  Serial.printf("📏 Need %d bytes, have %d bytes free\n", bodyLength, ESP.getFreeHeap());

  // Try to allocate memory
  uint8_t* full_body = (uint8_t*)malloc(bodyLength);
  
  if (!full_body) {
    Serial.println("❌ Not enough memory! Try reducing RECORD_SECONDS or reboot ESP32.");
    http.end();
    return;
  }

  Serial.println("✅ Memory allocated successfully!");

  // WAV header (use actual data size)
  uint8_t wav_header[44];
  writeWAVHeader(wav_header, actual_data_size, SAMPLE_RATE);

  // Assemble the request body
  int pos = 0;
  memcpy(full_body + pos, header.c_str(), header.length()); 
  pos += header.length();
  memcpy(full_body + pos, wav_header, 44); 
  pos += 44;
  memcpy(full_body + pos, audio_buffer, actual_data_size);  // Only send recorded data
  pos += actual_data_size;
  memcpy(full_body + pos, footer.c_str(), footer.length());

  // Play "thinking" message before sending
  Serial.println("🤔 Thinking...");

  // Send request
  Serial.println("📤 Sending audio to AI server...");
  Serial.println("⏳ This may take 10-20 seconds for transcription + TTS...");
  
  // Send the request (this will block)
  int responseCode = http.POST(full_body, bodyLength);
  String reply = http.getString();
  
  Serial.printf("📬 Response code: %d\n", responseCode);
  
  if (responseCode == 200) {
    processAIResponse(reply);
  } else {
    Serial.println("❌ Failed to send audio to server");
    Serial.println("Response: " + reply);
  }

  free(full_body);
  http.end();
  
  Serial.println("🔘 Ready for next question! Press button to record.");
}

// Extract response processing to separate function
void processAIResponse(String reply) {
  // Parse JSON response
  DynamicJsonDocument doc(2048);  // Increased size for AI response
  DeserializationError err = deserializeJson(doc, reply);
  
  if (!err && doc.containsKey("text")) {
    String transcript = doc["text"].as<String>();
    Serial.println("🎤 I heard: \"" + transcript + "\"");
    
    // Check if we have an AI response
    if (doc.containsKey("ai_response")) {
      String ai_response = doc["ai_response"].as<String>();
      Serial.println("🤖 Robot says: \"" + ai_response + "\"");
      
      // Check if there's audio to play
      if (doc.containsKey("has_audio") && doc["has_audio"] == true) {
        String audio_url = doc["audio_url"].as<String>();
        String full_audio_url = String(server_url).substring(0, String(server_url).lastIndexOf('/')) + audio_url;
        
        Serial.println("🔗 Audio URL from server: " + audio_url);
        Serial.println("🔗 Full URL: " + full_audio_url);
        Serial.println("🔗 Server base: " + String(server_url));
        
        playAudioFromURL(full_audio_url);
      } else {
        Serial.println("📝 No audio available, showing text only");
      }
      
      // Flash LED to indicate successful AI interaction
      for (int i = 0; i < 5; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(150);
        digitalWrite(LED_PIN, LOW);
        delay(150);
      }
    } else {
      // Just transcription
      Serial.println("📝 Transcription only");
    }
    
  } else {
    Serial.println("❌ Failed to parse AI response");
    Serial.println("Raw response: " + reply);
  }
}

// Download and play WAV audio using PWM
void playAudioFromURL(String url) {
  Serial.println("🔊 Downloading WAV audio from OpenAI...");
  
  // Ensure silence first
  noTone(AUDIO_OUT_PIN);
  digitalWrite(AUDIO_OUT_PIN, LOW);
  delay(200);
  
  WiFiClient client;
  HTTPClient http;
  http.begin(client, url);
  
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    Serial.printf("✅ HTTP Success, Content-Length: %d bytes\n", http.getSize());
    
    WiFiClient* stream = http.getStreamPtr();
    
    // Read enough bytes to find the actual header structure
    uint8_t header[100];
    int headerBytesRead = stream->readBytes(header, 100);
    
    // Find the "fmt " chunk to get audio format info
    int fmtPos = -1;
    for (int i = 0; i < headerBytesRead - 4; i++) {
      if (header[i] == 'f' && header[i+1] == 'm' && header[i+2] == 't' && header[i+3] == ' ') {
        fmtPos = i;
        break;
      }
    }
    
    if (fmtPos == -1) {
      Serial.println("❌ Couldn't find fmt chunk!");
      http.end();
      return;
    }
    
    Serial.printf("📄 Found fmt chunk at position %d\n", fmtPos);
    
    // Parse format chunk (starts 8 bytes after "fmt ")
    int formatStart = fmtPos + 8;
    uint16_t audioFormat = header[formatStart] | (header[formatStart+1] << 8);
    uint16_t numChannels = header[formatStart+2] | (header[formatStart+3] << 8);
    uint32_t sampleRate = header[formatStart+4] | (header[formatStart+5] << 8) | 
                         (header[formatStart+6] << 16) | (header[formatStart+7] << 24);
    uint32_t byteRate = header[formatStart+8] | (header[formatStart+9] << 8) | 
                       (header[formatStart+10] << 16) | (header[formatStart+11] << 24);
    uint16_t blockAlign = header[formatStart+12] | (header[formatStart+13] << 8);
    uint16_t bitsPerSample = header[formatStart+14] | (header[formatStart+15] << 8);
    
    // Debug the raw bytes being parsed
    Serial.printf("🔍 Format bytes at pos %d: ", formatStart);
    for (int i = 0; i < 16; i++) {
      Serial.printf("%02X ", header[formatStart + i]);
    }
    Serial.println();
    
    // Print actual format info
    Serial.printf("📊 Audio format: %d (1=PCM)\n", audioFormat);
    Serial.printf("📊 Channels: %d\n", numChannels);
    Serial.printf("📊 Sample rate: %d Hz\n", sampleRate);
    Serial.printf("📊 Byte rate: %d bytes/sec\n", byteRate);
    Serial.printf("📊 Block align: %d bytes\n", blockAlign);
    Serial.printf("📊 Bits per sample: %d\n", bitsPerSample);
    
    // Find "data" chunk
    int dataPos = -1;
    for (int i = fmtPos; i < headerBytesRead - 4; i++) {
      if (header[i] == 'd' && header[i+1] == 'a' && header[i+2] == 't' && header[i+3] == 'a') {
        dataPos = i;
        break;
      }
    }
    
    if (dataPos == -1) {
      Serial.println("❌ Couldn't find data chunk!");
      http.end();
      return;
    }
    
    Serial.printf("📄 Found data chunk at position %d\n", dataPos);
    
    // Data size is 4 bytes after "data"
    uint32_t dataSize = header[dataPos+4] | (header[dataPos+5] << 8) | 
                       (header[dataPos+6] << 16) | (header[dataPos+7] << 24);
    
    // Debug the data size bytes
    Serial.printf("🔍 Data size bytes at pos %d: %02X %02X %02X %02X\n", 
                  dataPos+4, header[dataPos+4], header[dataPos+5], header[dataPos+6], header[dataPos+7]);
    
    Serial.printf("📊 Data size: %d bytes (0x%08X)\n", dataSize, dataSize);
    
    // Sanity check - if data size is crazy, try to use file size instead
    if (dataSize > 1000000 || dataSize == 0) {
      Serial.println("❌ Data size looks wrong! Using HTTP content length instead.");
      dataSize = http.getSize() - 44; // Approximate: total size minus header
      Serial.printf("📊 Using estimated data size: %d bytes\n", dataSize);
    }
    
    // Audio data starts 8 bytes after "data"
    int audioDataStart = dataPos + 8;
    Serial.printf("📄 Audio data starts at byte %d\n", audioDataStart);
    
    // Calculate timing
    uint32_t samplePeriodMicros = 1000000 / sampleRate;
    float expectedDuration = (float)dataSize / (float)byteRate;
    Serial.printf("📊 Expected duration: %.2f seconds\n", expectedDuration);
    Serial.printf("⏱️ Sample period: %d microseconds\n", samplePeriodMicros);
    
    // Use streaming with small buffer instead of loading everything to memory
    Serial.println("🎵 Streaming audio with small buffer...");
    
    // Skip any remaining header bytes
    int bytesToSkip = audioDataStart - headerBytesRead;
    if (bytesToSkip > 0) {
      uint8_t skipBuffer[bytesToSkip];
      stream->readBytes(skipBuffer, bytesToSkip);
      Serial.printf("📄 Skipped %d additional header bytes\n", bytesToSkip);
    }
    
    // Play any audio data from the original header read
    int sampleCount = 0;
    unsigned long startMicros = micros();
    unsigned long playbackStart = millis();
    
    if (bytesToSkip < 0) {
      int audioFromHeader = headerBytesRead - audioDataStart;
      Serial.printf("📄 Playing %d audio bytes from header\n", audioFromHeader);
      
      for (int i = audioDataStart; i < headerBytesRead; i += 2) {
        if (i + 1 < headerBytesRead) {
          int16_t sample = header[i] | (header[i+1] << 8);
          uint8_t pwmValue = map(sample, -32768, 32767, 100, 155);
          
          // Precise timing
          unsigned long targetTime = startMicros + (sampleCount * samplePeriodMicros);
          while (micros() < targetTime) { /* wait */ }
          
          analogWrite(AUDIO_OUT_PIN, pwmValue);
          sampleCount++;
        }
      }
    }
    
    // Stream the rest with overlapped download/playback
    uint8_t buffer1[256];
    uint8_t buffer2[256];
    bool useBuffer1 = true;
    
    // Download first chunk
    int bytesRead1 = stream->readBytes(buffer1, sizeof(buffer1));
    Serial.printf("📥 Downloaded first chunk: %d bytes\n", bytesRead1);
    
    int totalBytesProcessed = 0;
    
    while (bytesRead1 > 0) {
      uint8_t* currentBuffer = useBuffer1 ? buffer1 : buffer2;
      int currentBytes = bytesRead1;
      
      Serial.printf("📦 Processing chunk: %d bytes\n", currentBytes);
      
      // Start downloading next chunk while playing current one
      int nextBytes = 0;
      if (http.connected()) {
        uint8_t* nextBuffer = useBuffer1 ? buffer2 : buffer1;
        nextBytes = stream->readBytes(nextBuffer, 256);
      }
      
      // Play current buffer
      for (int i = 0; i < currentBytes; i += 2) {
        if (i + 1 < currentBytes) {
          int16_t sample = currentBuffer[i] | (currentBuffer[i+1] << 8);
          
          // Much more sensitive PWM mapping for small audio values
          uint8_t pwmValue;
          if (abs(sample) < 20) {
            // For very quiet samples (silence), use true middle value
            pwmValue = 127;
          } else {
            pwmValue = map(sample, -2000, 2000, 80, 175);
            pwmValue = constrain(pwmValue, 80, 175);
          }
          
          // Force debug for first few samples of each chunk
          if (totalBytesProcessed < 1000 && (sampleCount % 10) == 0) {
            Serial.printf("🔍 Sample %d: raw=%d, pwm=%d\n", sampleCount, sample, pwmValue);
          }
          
          // FASTER timing - skip some waits to speed up playback
          unsigned long targetTime = startMicros + (sampleCount * (samplePeriodMicros * 7 / 12)); // ~0.58x speed
          if (micros() < targetTime) {
            while (micros() < targetTime) { /* wait */ }
          }
          
          analogWrite(AUDIO_OUT_PIN, pwmValue);
          sampleCount++;
        }
      }
      
      totalBytesProcessed += currentBytes;
      
      // Switch buffers
      useBuffer1 = !useBuffer1;
      bytesRead1 = nextBytes;
    }
    
    Serial.printf("📊 Total bytes processed: %d\n", totalBytesProcessed);
    
    unsigned long playbackEnd = millis();
    float actualTime = (playbackEnd - playbackStart) / 1000.0;
    Serial.printf("✅ Played %d samples in %.2f seconds\n", sampleCount, actualTime);
    Serial.printf("📊 Expected: %.2f sec, Actual: %.2f sec\n", expectedDuration, actualTime);
    
  } else {
    Serial.printf("❌ HTTP Error: %d\n", httpCode);
  }
  
  http.end();
  
  // FORCE complete silence for the dog!
  Serial.println("🔇 Forcing complete silence...");
  noTone(AUDIO_OUT_PIN);           // Stop any tone generation
  analogWrite(AUDIO_OUT_PIN, 0);   // Set PWM to 0
  delay(100);
  digitalWrite(AUDIO_OUT_PIN, LOW); // Set pin to digital LOW
  delay(100);
  
  // Triple check silence
  pinMode(AUDIO_OUT_PIN, OUTPUT);
  digitalWrite(AUDIO_OUT_PIN, LOW);
  
  Serial.println("🔇 Pin forced to LOW - should be completely silent now");
  Serial.println("🐕 Dog-safe mode activated!");
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