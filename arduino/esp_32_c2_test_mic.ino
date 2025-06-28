#include <driver/i2s.h>

#define SAMPLE_RATE     16000
#define I2S_NUM         I2S_NUM_0
#define READ_LEN        1024

// I2S mic pin config for ESP32-C3
i2s_config_t i2s_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = I2S_COMM_FORMAT_I2S_MSB,
  .intr_alloc_flags = 0,
  .dma_buf_count = 4,
  .dma_buf_len = 512,
  .use_apll = false,
  .tx_desc_auto_clear = false,
  .fixed_mclk = 0
};

i2s_pin_config_t pin_config = {
  .bck_io_num = 1,    // SCK  (Bit Clock)
  .ws_io_num = 2,     // WS   (Word Select / L/R clock)
  .data_out_num = I2S_PIN_NO_CHANGE,
  .data_in_num = 0    // SD   (Serial Data)
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("🔧 Starting setup");

  esp_err_t err;

  Serial.println("🔧 Installing I2S driver...");
  err = i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("❌ Failed installing I2S driver: %d\n", err);
    while (true);
  }

  Serial.println("🔧 Setting I2S pins...");
  err = i2s_set_pin(I2S_NUM, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("❌ Failed setting I2S pins: %d\n", err);
    while (true);
  }

  Serial.println("✅ I2S init complete!");
}

void loop() {
  int16_t samples[READ_LEN];
  size_t bytes_read;

  // Read samples from the mic
  esp_err_t result = i2s_read(I2S_NUM, samples, READ_LEN * sizeof(int16_t), &bytes_read, portMAX_DELAY);

  if (result == ESP_OK && bytes_read > 0) {
    size_t sample_count = bytes_read / sizeof(int16_t);
    long sum = 0;

    for (size_t i = 0; i < sample_count; i++) {
      sum += abs(samples[i]);
    }

    int avg_signal = sum / sample_count;
    Serial.printf("🔊 Avg signal: %d\n", avg_signal);
  } else {
    Serial.println("⚠️ I2S read error");
  }

  delay(100);
}
