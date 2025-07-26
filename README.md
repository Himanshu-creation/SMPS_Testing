#  SMPS Testing System using ESP32-S3
This project is a **smart automated SMPS testing system** using **ESP32-S3**, capable of testing up to **8 SMPS units** by monitoring their voltage output, counting ON/OFF cycles, and logging temperature data. It features control via a **web interface**.

---

##  Features

- Supports up to **8 SMPS units** via relay control  
- Automatically toggle each SMPS with user-defined intervals  
- Monitor voltage output using **CD4051 analog multiplexer**  
- Temperature measurement via **MAX31856** thermocouple amplifier   
- Displays **ON/OFF cycle count** per SMPS  
- Optional **ESP32-hosted web interface**

---

## Hardware Used

| Component              | Quantity | Description                                    |
|------------------------|----------|------------------------------------------------|
| ESP32-S3               | 1        | Wi-Fi capable microcontroller                  |
| CD4051                 | 1        | 8-channel analog multiplexer                   |
| MAX31856               | 1        | Thermocouple amplifier (SPI interface)         |
| K-Type Thermocouple    | 1        | For temperature sensing                        |
| Relays (5V, 10M cycles)| 8        | For switching SMPS units                       |
| Voltage Divider (R1=39k, R2=3.3k)| 8 | Scales ~40V to 3.3V for ADC                 |
| BMS Board              | 1        | For protection and auto-switching              |
| Onboard SMPS           | 1        | 5V 4A for powering the system                  |

---

## Voltage Divider Design

To scale ~40V to 3.3V for ADC input:
R1 = 39kΩ, R2 = 3.3kΩ
Vout = Vin × (R2 / (R1 + R2))

## Web Interface (Optional)

If enabled in the ESP32 firmware, access your ESP32’s IP to:
- Toggle individual relays
- Monitor SMPS voltages
- View ON/OFF duration
- View cycle count

---
## Getting Started

1. **Upload** the Arduino code from `firmware/` to ESP32-S3
2. **Power** the board via onboard 5V SMPS and verify relay switching
3. **Connect** SMPS outputs to CD4051 (via voltage dividers)
4. **Start testing** via web interface

## 📦 Dependencies

Make sure the following libraries are installed in Arduino IDE:

- `ESPAsyncWebServer`
- `Adafruit_MAX31856`
- `SPI`
- `WiFi`

---
