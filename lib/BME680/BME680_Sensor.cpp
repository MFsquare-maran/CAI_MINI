#include "BME680_Sensor.h"

BME680_Sensor::BME680_Sensor(uint8_t addr) : _address(addr) {}

bool BME680_Sensor::begin() {
    Wire.begin();

    while (!_bme.begin(_address)) {
        logln("❌ BME680 nicht gefunden!");
        delay(1000); // Warte vor erneutem Versuch
    }

    logln("✅ BME680 initialisiert!");

    _bme.setTemperatureOversampling(BME680_OS_8X);
    _bme.setHumidityOversampling(BME680_OS_2X);
    _bme.setPressureOversampling(BME680_OS_4X);
    _bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    _bme.setGasHeater(320, 150);

    return true;
}

bool BME680_Sensor::readSensor() {
    if (!_bme.performReading()) {
        logln("⚠️ Fehler beim Auslesen des BME680!");
        return false;
    }
    return true;
}

float BME680_Sensor::getTemperature() const {
    return _bme.temperature+temperature_Offset;
}

float BME680_Sensor::getPressure() const {
    return _bme.pressure / 100.0+pressure_Offset; // hPa
}

float BME680_Sensor::getHumidity() const {
    return _bme.humidity+humidity_Offset;
}

float BME680_Sensor::getGasResistance() const {
    return _bme.gas_resistance / 1000.0+gas_resistance_Offset; // kΩ
}

void BME680_Sensor::set_offset(float temperature,float pressure,float huminity,float gas ){
    temperature_Offset = temperature;
    pressure_Offset = pressure;
    humidity_Offset = huminity;
    gas_resistance_Offset = gas;
}

void BME680_Sensor::enable() {
    logln("🟢 BME680 enabled");

    _bme.setTemperatureOversampling(BME680_OS_8X);
    _bme.setHumidityOversampling(BME680_OS_2X);
    _bme.setPressureOversampling(BME680_OS_4X);

    _bme.setIIRFilterSize(BME680_FILTER_SIZE_3);

    // Heater wieder aktivieren
    _bme.setGasHeater(320, 150);

    _enabled = true;
}

void BME680_Sensor::disable() {
    logln("🔴 BME680 disabled");

    // Heater AUS
    _bme.setGasHeater(0, 0);

    // Optional: Oversampling runter = weniger Strom
    _bme.setTemperatureOversampling(BME680_OS_1X);
    _bme.setHumidityOversampling(BME680_OS_1X);
    _bme.setPressureOversampling(BME680_OS_1X);

    _enabled = false;
}

bool BME680_Sensor::isEnabled() {
    return _enabled;
}
