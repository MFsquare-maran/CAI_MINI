# CAI_MINI — Environmental Sensor Platform

> Modulare IoT-Sensorplattform auf Basis des **Seeed XIAO ESP32-S3**  
> zur Erfassung von Umweltdaten und Übertragung via **WLAN/MQTT** oder **LoRa**.

---

## 📋 Inhaltsverzeichnis

- [Übersicht](#übersicht)
- [Hardware](#hardware)
- [Projektstruktur](#projektstruktur)
- [Environments / Betriebsmodi](#environments--betriebsmodi)
- [Konfiguration (INI-Datei)](#konfiguration-ini-datei)
- [Bibliotheken / Dependencies](#bibliotheken--dependencies)
- [Eigene Klassen](#eigene-klassen)
- [Ablauf WLAN-Modus](#ablauf-wlan-modus)
- [Datenlogging (SD-Karte)](#datenlogging-sd-karte)
- [Firmware-Update (OTA)](#firmware-update-ota)
- [LED-Statusanzeige](#led-statusanzeige)
- [Build & Flash](#build--flash)
- [Autor](#autor)

---

## 📡 Übersicht

**CAI_MINI** ist eine modulare, energieeffiziente IoT-Sensorplattform.  
Das System erfasst Umweltdaten (Temperatur, Luftdruck, Luftfeuchtigkeit, Gaswiderstand)  
mithilfe eines **BME680-Sensors** und überträgt diese entweder direkt via **WLAN zu ThingsBoard**  
oder über ein **LoRa-Mesh-Netzwerk** (Sensor → Router → Gateway).

Das Gerät ist für einen **Single-Shot-Betrieb** ausgelegt:
- Aufwachen
- Daten erfassen
- Daten senden
- Kontrolliertes Herunterfahren

---

## 🔧 Hardware

| Komponente         | Beschreibung                              |
|--------------------|-------------------------------------------|
| **MCU**            | Seeed XIAO ESP32-S3                       |
| **Sensor**         | BME680 (Temperatur, Druck, Feuchte, Gas)  |
| **Speicher**       | SD-Karte (SPI) — CSV Datenlogging         |
| **Kommunikation**  | WLAN (802.11 b/g/n) **oder** LoRa         |
| **Stromversorgung**| LiPo Akku (3.5V – 4.2V, ADC-Messung)     |
| **Konfiguration**  | INI-Datei auf SD-Karte (`/INIT.ini`)      |
| **Status-LEDs**    | LED Orange (WLAN), LED Blau (Datensendung)|

---

## 📁 Projektstruktur

```
CAI_MINI/
│
├── platformio.ini                  ← Build-Konfiguration (alle Environments)
│
├── src/
│   ├── WLAN/
│   │   └── main.cpp                ← WLAN-Modus: Sensor → ThingsBoard via MQTT
│   ├── LORA_SENSOR/
│   │   └── main.cpp                ← LoRa Sensor: Daten erfassen & per LoRa senden
│   ├── LORA_ROUTER/
│   │   └── main.cpp                ← LoRa Router: Pakete weiterleiten
│   └── LORA_GATEWAY/
│       └── main.cpp                ← LoRa Gateway: Pakete empfangen & ins Netz weiterleiten
│
├── lib/
│   └── Klassen/
│       ├── BME680_Sensor.h         ← BME680 Sensor-Wrapper (Header)
│       ├── BME680_Sensor.cpp       ← BME680 Sensor-Wrapper (Implementierung)
│       ├── FirmwareUpdater.h       ← OTA Firmware-Update via ThingsBoard (Header)
│       └── FirmwareUpdater.cpp     ← OTA Firmware-Update via ThingsBoard (Implementierung)
│
└── include/
    └── pin_config.h                ← Pin-Definitionen (SD, LEDs, Akku, Shutdown)
```

---

## ⚙️ Environments / Betriebsmodi

Das Projekt nutzt **PlatformIO Environments** um vier unabhängige Firmwares  
aus derselben Codebase zu bauen:

| Environment           | Beschreibung                                                  |
|-----------------------|---------------------------------------------------------------|
| `CAI_MINI_WLAN`       | WLAN-Verbindung, Sensordaten direkt an ThingsBoard via MQTT   |
| `CAI_MINI_LORA_SENSOR`| Sensordaten erfassen und per LoRa senden                      |
| `CAI_MINI_LORA_ROUTER`| LoRa-Pakete von Sensor empfangen und weiterleiten             |
| `CAI_MINI_LORA_GATEWAY`| LoRa-Pakete empfangen und ins Backend (WLAN/MQTT) weiterleiten|

```ini
[env:CAI_MINI_WLAN]
platform = espressif32
board = seeed_xiao_esp32s3
framework = arduino
monitor_speed = 115200
build_src_filter =
    -<*>
    +<WLAN/>
lib_deps = ...

[env:CAI_MINI_LORA_SENSOR]
platform = espressif32
board = seeed_xiao_esp32s3
framework = arduino
monitor_speed = 115200
build_src_filter =
    -<*>
    +<LORA_SENSOR/>
lib_deps = ...
```

---

## 🗂️ Konfiguration (INI-Datei)

Alle Zugangsdaten und Kalibrierungswerte werden **nicht im Code hardcodiert**,  
sondern aus der Datei `/INIT.ini` auf der SD-Karte gelesen.

**Beispiel `/INIT.ini`:**

```ini
[WIFI]
SSID       = MeinNetzwerk
PW         = MeinPasswort

[THINGSBOARD]
TB_ADRESS  = thingsboard.example.com
TB_TOKKEN  = mein-access-token
TB_PORT    = 1883

[BME680]
TEMPERATURE_OFFSET = -1.5
PRESSURE_OFFSET    = 0.0
HUMINITY_OFFSET    = 2.0
GAS_OFFSET         = 0.0
```

> ⚠️ **Wichtig:** Die `INIT.ini` niemals in ein öffentliches Git-Repository committen!  
> In `.gitignore` eintragen.

---

## 📦 Bibliotheken / Dependencies

| Bibliothek                        | Zweck                              |
|-----------------------------------|------------------------------------|
| `adafruit/Adafruit BME680 Library`| BME680 Sensor Treiber              |
| `adafruit/Adafruit Unified Sensor`| Adafruit Sensor-Abstraktionsschicht|
| `stevemarple/IniFile`             | INI-Datei Parsing von SD-Karte     |
| `thingsboard/ThingsBoard`         | ThingsBoard MQTT Client            |
| `thingsboard/TBPubSubClient`      | MQTT PubSub Transport              |
| `bblanchon/ArduinoJson`           | JSON Serialisierung / Parsing      |

---

## 🧩 Eigene Klassen

### `BME680_Sensor`

Wrapper-Klasse für den Adafruit BME680 Sensor.

| Methode | Beschreibung |
|---|---|
| `BME680_Sensor(uint8_t address)` | Konstruktor mit I²C Adresse |
| `begin()` | Sensor initialisieren |
| `set_offset(temp, press, hum, gas)` | Kalibrierungsoffsets setzen |
| `readSensor()` | Messung durchführen |
| `getTemperature()` | Temperatur in °C |
| `getPressure()` | Luftdruck in hPa |
| `getHumidity()` | Luftfeuchtigkeit in % |
| `getGasResistance()` | Gaswiderstand in kΩ |

---

### `FirmwareUpdater`

OTA-Firmware-Update Klasse via ThingsBoard.

| Methode | Beschreibung |
|---|---|
| `FirmwareUpdater(server, token, version)` | Konstruktor |
| `checkAndUpdate()` | Prüft auf neue Firmware, lädt sie herunter und startet neu |

---

## 🔄 Ablauf WLAN-Modus

```
Einschalten
    │
    ▼
SD-Karte initialisieren
    │
    ▼
FreeRTOS Timer-Task starten (Watchdog → Shutdown nach X Sekunden)
    │
    ▼
BME680 initialisieren
    │
    ▼
INI-Datei lesen (SSID, Passwort, ThingsBoard, Offsets)
    │
    ▼
Sensor-Offsets setzen
    │
    ▼
WLAN verbinden (Timeout: 15s)
    │
    ▼
Firmware-Update prüfen (OTA via ThingsBoard)
    │
    ▼
NTP Zeit synchronisieren (ch.pool.ntp.org, UTC+1)
    │
    ▼
ThingsBoard verbinden
    │
    ▼
BME680 auslesen + Akku-Spannung messen
    │
    ▼
Daten auf SD-Karte loggen (CSV: /data.csv)
    │
    ▼
Telemetrie & Attribute an ThingsBoard senden
    │   • Temperature, Pressure, Humidity, Gas_Resistance
    │   • Battery_Voltage, RSSI, SSID, IP, FW-Version
    ▼
Kontrolliertes System-Shutdown
```

---

## 💾 Datenlogging (SD-Karte)

Messdaten werden automatisch in `/data.csv` auf der SD-Karte gespeichert.  
Der CSV-Header wird nur beim ersten Schreiben angelegt.

**Format:**

```csv
Date,Temperature,Pressure,Humidity,Gas_Resistance,Battery_Voltage
2026-04-01 17:10:34,22.45,1013.25,48.30,125.60,3.87
```

---

## 🔋 Akkuspannung

Die Akkuspannung wird per **ADC** gemessen und mittels **linearer Interpolation**  
einer vordefinierten Lookup-Tabelle in Volt umgerechnet:

| ADC-Wert | Spannung |
|----------|----------|
| 2644     | 3.5 V    |
| 2720     | 3.6 V    |
| ...      | ...      |
| 3249     | 4.2 V    |

Werte außerhalb des Bereichs werden **extrapoliert**.

---

## 🔁 Firmware-Update (OTA)

Der `FirmwareUpdater` prüft nach der WLAN-Verbindung automatisch,  
ob ThingsBoard eine neue Firmware bereitstellt.

- ✅ Neue Version verfügbar → Download → Flash → Neustart
- ✅ Gleiche Version → Normaler Betrieb

Die aktuelle Firmware-Version wird über das Define `FW_VERSION` gesetzt  
(z.B. in `pin_config.h`).

---

## 🟠🔵 LED-Statusanzeige

| LED        | Zustand        | Bedeutung                    |
|------------|----------------|------------------------------|
| 🟠 Orange  | Blinkt         | WLAN-Verbindung wird aufgebaut |
| 🟠 Orange  | Dauerhaft AN   | WLAN-Verbindungsfehler       |
| 🟠 Orange  | AUS            | WLAN verbunden               |
| 🔵 Blau    | AN             | Daten werden gesendet        |
| 🔵 Blau    | AUS            | Senden abgeschlossen         |

---

## 🚀 Build & Flash

```bash
# WLAN Firmware bauen
pio run -e CAI_MINI_WLAN

# WLAN Firmware flashen
pio run -e CAI_MINI_WLAN --target upload

# LoRa Sensor Firmware bauen & flashen
pio run -e CAI_MINI_LORA_SENSOR --target upload

# Alle Environments bauen
pio run

# Serial Monitor öffnen
pio device monitor -e CAI_MINI_WLAN --baud 115200

# Build-Cache leeren
pio run --target clean
```

---

## 👤 Autor

**maran**  
Erstellt: 2026-04-01  
Plattform: [PlatformIO](https://platformio.org/) + [Arduino Framework](https://www.arduino.cc/)  
Board: [Seeed XIAO ESP32-S3](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
