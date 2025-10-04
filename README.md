# 🧠 ESP32 Voice Input Project

This project captures audio using an INMP441 microphone connected to an ESP32-C3 board, processes it (e.g., sends it to AssemblyAI), and displays results (such as transcribed text or LED feedback). The goal is to have it send the transcribed text to an AI, have the AI respond, and use text-to-speech to output the sound as well.

---

## 🛠️ Hardware

- ESP32-WROOM-32 or ESP32-C3 SuperMini
- INMP441 I2S Microphone
- (Optional) LEDs with resistors
- Breadboard + jumper wires
- USB-C cable

---

## 🔌 Wiring

### INMP441 to ESP32-WROOM-32
| INMP441 Pin | ESP32 Pin |
| ----------- | --------- |
| VCC         | 3.3V      |
| GND         | GND       |
| WS          | GPIO15    |
| SCK         | GPIO14    |
| SD          | GPIO32    |

### INMP441 to ESP32-C3

| INMP441 Pin | ESP32-C3 GPIO |
|-------------|----------------|
| VCC         | 3.3V           |
| GND         | GND            |
| WS          | GPIO 2         |
| SCK         | GPIO 1         |
| SD          | GPIO 0         |

### Button and Built-in LED

| Component        | ESP32-C3 GPIO |
|------------------|----------------|
| Button           | GPIO 3         |
| Built-in LED     | GPIO 8 (onboard)|

### Audio Output (MAX98357A) - Optional

| MAX98357A Pin | ESP32-C3 GPIO |
|---------------|----------------|
| DIN           | GPIO 7         |
| BCLK          | GPIO 9         |
| LRC           | GPIO 10        |

---

## 🧰 Arduino IDE Setup

1. Open **Arduino IDE**
2. Go to `File > Preferences`
   - In *Additional Board URLs*, add:  
     `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Go to `Tools > Board > Boards Manager`
   - Search for `esp32` and install it (by Espressif Systems)
4. Go to `Tools > Board` and select:
   - Go to Tools → Select your board:
    -> **ESP32 Dev Module** for ESP32-WROOM-32
    -> **ESP32C3 Dev Module** for ESP32-C3
5. Go to `Tools > Port` and select the port labeled something like:
   - `/dev/ttyUSB0` or `usbserial-####`
6. Recommended settings:
   - Flash Size: 4MB
   - Upload Speed: 115200
   - Erase Flash: "All Flash Contents" (only if needed)

---

## 🚀 Upload

1. Plug in your ESP32-C3
2. Click ✅ Upload in Arduino
3. If upload fails, press and hold **BOOT** while clicking Upload

---

## 🧪 Serial Monitor

- Open `Tools > Serial Monitor`
- Set **baud rate** to `115200`
- Look for logs like:
