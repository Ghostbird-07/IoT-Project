# 🏫 Smart Classroom Occupancy System

**CS-744 Internet of Things — Project Report**  
**NIT Hamirpur | Anshul (22DCS003)**  
**Under the guidance of Dr. Robin Singh Bhadoria**

---

## 📌 Overview

An **IoT-based embedded system** built on the **ESP32** platform that:

- 🪪 **Automates student attendance** using RFID (MFRC522)
- 📊 **Tracks real-time classroom occupancy** with graduated alerts
- 🌡️ **Monitors environment** — Temperature, Humidity, Air Quality (CO₂)
- ☁️ **Streams live telemetry** to ThingsBoard cloud via MQTT
- 🔔 **Triggers alerts** for overcrowding (`> 30 students`) and bad air quality (`ADC > 2500`)

Validated on the **Wokwi** simulation platform. Total hardware cost **< ₹1,500**.

---

## 🗂️ Repository Structure

```
├── src/
│   └── main.cpp          # ESP32 firmware (C++ / PlatformIO)
├── diagram.json          # Wokwi circuit diagram
├── platformio.ini        # PlatformIO build config
├── wokwi.toml            # Wokwi project config
├── sketch.ino            # Arduino IDE compatible sketch
├── libraries.txt         # Required Arduino libraries
├── report.tex            # Full LaTeX project report (with TikZ diagrams)
├── IoT.pdf               # Reference sample report
├── Smart_Classroom_PPT.pptx  # Project presentation
└── make_ppt.py           # Script used to generate the PPTX
```

---

## ⚙️ Hardware Components

| Component | Function | Interface | Cost (₹) |
|---|---|---|---|
| ESP32 DevKit V1 | Central MCU / Wi-Fi | — | 450 |
| MFRC522 RFID | Student ID scanning | SPI | 150 |
| DHT22 | Temperature + Humidity | GPIO 15 | 350 |
| MQ-135 | Air Quality / CO₂ | ADC 34 | 120 |
| HC-SR501 PIR | Motion detection | GPIO 27 | 90 |
| LCD 16×2 I²C | Display | I²C 0x27 | 150 |
| Piezo Buzzer | Audio alerts | GPIO 26 | 40 |
| **Total** | | | **< ₹1,500** |

---

## 🚀 Getting Started

### Simulate on Wokwi
1. Go to [wokwi.com](https://wokwi.com) and open a new ESP32 project.
2. Replace the sketch with `src/main.cpp` and the diagram with `diagram.json`.
3. Press **Play** ▶️ to simulate.

### Build with PlatformIO
```bash
# Install PlatformIO CLI, then:
pio run          # Build
pio run -t upload  # Flash to hardware
```

### Required Libraries (`libraries.txt`)
```
MFRC522
DHT sensor library
LiquidCrystal I2C
ArduinoJson
PubSubClient
```

---

## ☁️ Cloud Setup (ThingsBoard)

1. Create a free account at [thingsboard.io](https://thingsboard.io).
2. Add a new **Device** and copy the **Access Token**.
3. In `src/main.cpp`, replace `YOUR_DEVICE_ACCESS_TOKEN` with your token.
4. Data streams to the `v1/devices/me/telemetry` MQTT topic.

---

## 📄 Report

The full academic report is in [`report.tex`](report.tex). Compile with:

```bash
pdflatex report.tex
pdflatex report.tex   # run twice for TOC
```

Or upload to [Overleaf](https://overleaf.com) for instant online compilation.

The report includes:
- TikZ hardware architecture diagram
- Firmware FSM diagram  
- Full control-loop flowchart
- Code listings with syntax highlighting
- Validation test matrix and Bill of Materials

---

## 📋 Alert Logic

```
Alert = OVERCROWD   if Occupancy > 30
      = BAD_AIR     if MQ-135 ADC > 2500
      = NORMAL      otherwise
```

| Condition | LCD Output | Buzzer |
|---|---|---|
| Valid entry | `Welcome, Name!` | 1 beep |
| Valid exit | `Goodbye, Name!` | 2 beeps |
| Unknown card | `Access Denied` | 3 beeps |
| Overcrowding | `!! WARNING !!` | 5 rapid beeps |
| Bad air quality | `BAD AIR QUALITY` | Continuous |

---

## 📜 License

MIT License © 2026 Anshul (22DCS003), NIT Hamirpur
