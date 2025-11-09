#include "lvgl.h"
#include "Display_ST7789.h"
#include "LVGL_Driver.h"
#include "ui/ui.h"
#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include "ScioSense_ENS160.h"

// WiFi Configuration (Multiple Networks)
struct WiFiCredentials
{
    const char *ssid;
    const char *password;
};

WiFiCredentials wifiNetworks[] = {
    {"U+Net6C15", "41250546M!"},
    {"Stonei", "Dsji0201!@"}};
const int numNetworks = 2;

// NTP Server Configuration
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 32400;
const int daylightOffset_sec = 0;

// I2C Pin Definition
#define I2C_SDA 0
#define I2C_SCL 1

// Sensors
Adafruit_AHTX0 aht;
ScioSense_ENS160 ens160(ENS160_I2CADDR_1); // Try 0x52 address first

static uint32_t last_tick = 0;
static uint32_t last_time_update = 0;
static uint32_t last_sensor_update = 0;

bool aht_available = false;
bool ens160_available = false;
uint32_t ens160_warmup_start = 0;

// Weekday String Array (English)
const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// Update DateTime Label Function
void update_datetime_label()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        Serial.println("Failed to obtain time");
        lv_label_set_text(ui_hourminsecond, "Time Sync Failed");
        return;
    }

    // Date + Weekday Display
    char dateStr[64];
    snprintf(dateStr, sizeof(dateStr), "%04d.%02d.%02d (%s)",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             weekdays[timeinfo.tm_wday]);
    lv_label_set_text(ui_yearmonthdate, dateStr);

    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    lv_label_set_text(ui_hourminsecond, timeStr);
}

// I2C Scan
void scan_i2c_devices()
{
    Serial.println("\n╔════════════════════════════════╗");
    Serial.println("║    I2C Device Scan             ║");
    Serial.println("╚════════════════════════════════╝");
    byte count = 0;

    for (byte address = 1; address < 127; address++)
    {
        Wire.beginTransmission(address);
        byte error = Wire.endTransmission();

        if (error == 0)
        {
            Serial.print("✓ 0x");
            if (address < 16)
                Serial.print("0");
            Serial.print(address, HEX);

            if (address == 0x38)
                Serial.print(" → AHT21");
            if (address == 0x52)
                Serial.print(" → ENS160 (ADD=VCC)");
            if (address == 0x53)
                Serial.print(" → ENS160 (ADD=GND)");

            Serial.println();
            count++;
        }
    }

    Serial.print("\nTotal ");
    Serial.print(count);
    Serial.println(" device(s) found\n");
}

// AQI Level (Adafruit Library)
const char *getAQILevel(uint8_t aqi)
{
    if (aqi == 1)
        return "Excellent";
    else if (aqi == 2)
        return "Good";
    else if (aqi == 3)
        return "Moderate";
    else if (aqi == 4)
        return "Poor";
    else if (aqi == 5)
        return "Unhealthy";
    else
        return "Measuring";
}

// Read Sensor Data
void read_sensor_data()
{
    Serial.println("\n╔════════════════════════════════╗");
    Serial.println("║      Sensor Data               ║");
    Serial.println("╚════════════════════════════════╝");

    float temp = 0, humid = 0;

    // AHT21
    if (aht_available)
    {
        sensors_event_t humidity, temperature;
        aht.getEvent(&humidity, &temperature);
        temp = temperature.temperature;
        humid = humidity.relative_humidity;

        Serial.println("\n[AHT21]");
        Serial.print("  Temperature: ");
        Serial.print(temp, 1);
        Serial.println(" °C");
        Serial.print("  Humidity: ");
        Serial.print(humid, 1);
        Serial.println(" %");

        if (ui_temperaturevalue)
        {
            int16_t temp_tenths = (int16_t)(temp * 10.0f + (temp >= 0 ? 0.5f : -0.5f));
            int16_t temp_whole = temp_tenths / 10;
            int16_t temp_fraction = abs(temp_tenths % 10);
            char temp_buf[12];
            snprintf(temp_buf, sizeof(temp_buf), "%d.%dC", temp_whole, temp_fraction);
            lv_label_set_text(ui_temperaturevalue, temp_buf);
        }

        if (ui_humitygraph && ui_humityvalue)
        {
            int32_t arc_value = (int32_t)(humid + 0.5f);
            if (arc_value < 0)
                arc_value = 0;
            if (arc_value > 100)
                arc_value = 100;
            lv_arc_set_value(ui_humitygraph, arc_value);
            int16_t humid_tenths = (int16_t)(humid * 10.0f + (humid >= 0 ? 0.5f : -0.5f));
            int16_t whole = humid_tenths / 10;
            int16_t fraction = abs(humid_tenths % 10);
            char humid_buf[8];
            snprintf(humid_buf, sizeof(humid_buf), "%d.%d%%", whole, fraction);
            lv_label_set_text(ui_humityvalue, humid_buf);
        }
    }
    else
    {
        if (ui_temperaturevalue)
        {
            lv_label_set_text(ui_temperaturevalue, "--.-C");
        }

        if (ui_humitygraph && ui_humityvalue)
        {
            lv_arc_set_value(ui_humitygraph, 0);
            lv_label_set_text(ui_humityvalue, "--%");
        }
    }

    // ENS160
    if (ens160_available)
    {
        Serial.println("\n[ENS160]");

        uint32_t elapsed = (millis() - ens160_warmup_start) / 1000;

        Serial.print("  Elapsed: ");
        Serial.print(elapsed);
        Serial.print("s");

        if (elapsed < 180)
        {
            Serial.print(" | Warmup: ");
            Serial.print((elapsed * 100) / 180);
            Serial.print("%");
        }
        else
        {
            Serial.print(" | ✓ Warmup Complete");
        }
        Serial.println();

        // Temperature & Humidity Compensation
        if (aht_available)
        {
            // Convert to ENS210 format: T = (temp + 273.15) * 64, H = humidity * 512
            uint16_t t_ens210 = (uint16_t)((temp + 273.15) * 64);
            uint16_t h_ens210 = (uint16_t)(humid * 512);
            ens160.set_envdata210(t_ens210, h_ens210);
        }

        // Measure sensor data
        if (ens160.available())
        {
            ens160.measure(true);

            uint8_t aqi = ens160.getAQI();
            uint16_t tvoc = ens160.getTVOC();
            uint16_t eco2 = ens160.geteCO2();

            Serial.print("  AQI: ");
            Serial.print(aqi);
            Serial.print(" (");
            Serial.print(getAQILevel(aqi));
            Serial.println(")");

            Serial.print("  TVOC: ");
            Serial.print(tvoc);
            Serial.println(" ppb");

            Serial.print("  eCO2: ");
            Serial.print(eco2);
            Serial.println(" ppm");

            // Update UI - AQI
            if (ui_aqivalue)
            {
                char aqi_buf[8];
                snprintf(aqi_buf, sizeof(aqi_buf), "%u", (unsigned int)aqi);
                lv_label_set_text(ui_aqivalue, aqi_buf);
            }

            // Update UI - CO2
            if (ui_co2value)
            {
                char co2_buf[12];
                snprintf(co2_buf, sizeof(co2_buf), "%u ppm", (unsigned int)eco2);
                lv_label_set_text(ui_co2value, co2_buf);
            }
        }
        else
        {
            Serial.println("  ⚠️  Sensor not available");
        }
    }
    else
    {
        if (ui_aqivalue)
        {
            lv_label_set_text(ui_aqivalue, "--");
        }
        if (ui_co2value)
        {
            lv_label_set_text(ui_co2value, "-- ppm");
        }
    }

    Serial.println("\n════════════════════════════════\n");
}

void setup()
{
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n╔════════════════════════════════════════╗");
    Serial.println("║   ESP32-C6 Sensor System Starting    ║");
    Serial.println("╚════════════════════════════════════════╝");

    // I2C Initialization
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000); // 100kHz
    Serial.println("\n✓ I2C Initialized (100kHz)");

    scan_i2c_devices();

    // AHT21 Initialization
    Serial.print("AHT21 Initializing... ");
    if (aht.begin())
    {
        aht_available = true;
        Serial.println("✓ Success!");
    }
    else
    {
        Serial.println("✗ Failed!");
    }

    // ENS160 Initialization (ScioSense Library)
    Serial.println("\nENS160 Initializing:");
    Serial.println("  Trying I2C address 0x52...");

    bool init_result = ens160.begin();
    Serial.print("  begin() returned: ");
    Serial.println(init_result ? "true" : "false");

    delay(100);

    bool avail_result = ens160.available();
    Serial.print("  available() returned: ");
    Serial.println(avail_result ? "true" : "false");

    if (avail_result)
    {
        ens160_available = true;
        Serial.println("\n✓ ENS160 Detected!");

        // Print ENS160 versions
        Serial.print("  Firmware Rev: ");
        Serial.print(ens160.getMajorRev());
        Serial.print(".");
        Serial.print(ens160.getMinorRev());
        Serial.print(".");
        Serial.println(ens160.getBuild());

        Serial.println("\n  Sensor Configuration:");

        // Operating Mode Setting
        Serial.print("    Setting Standard Mode... ");
        bool mode_result = ens160.setMode(ENS160_OPMODE_STD);
        Serial.println(mode_result ? "✓ OK" : "✗ Failed");
        delay(100);

        // Start Warmup Timer
        ens160_warmup_start = millis();

        Serial.println("\n  ✓ Initialization Complete");
        Serial.println("  ⏳ Warmup ~3 minutes required");
        Serial.println("  ℹ️  Initial values may be inaccurate");
    }
    else
    {
        Serial.println("\n✗ ENS160 Not Found!");
        Serial.println("\n  Troubleshooting:");
        Serial.println("    1. Check if ENS160 appears in I2C scan above");
        Serial.println("    2. Verify wiring:");
        Serial.println("       VCC → 3.3V");
        Serial.println("       GND → GND");
        Serial.println("       SDA → GPIO 0");
        Serial.println("       SCL → GPIO 1");
        Serial.println("    3. Try different I2C address (0x52 or 0x53)");
        Serial.println("    4. Check if sensor is powered on");
    }

    Serial.println();

    // LCD Initialization
    Serial.println("LCD Initializing...");
    Lvgl_Init();
    lcd_set_brightness(10);
    Serial.println("✓ LCD Initialized");

    // WiFi - Multiple Network Attempt
    Serial.println("\nWiFi Connection Attempt:");
    bool wifi_connected = false;

    for (int i = 0; i < numNetworks; i++)
    {
        Serial.print("  ");
        Serial.print(wifiNetworks[i].ssid);
        Serial.print(" Connecting");

        WiFi.begin(wifiNetworks[i].ssid, wifiNetworks[i].password);
        int wifi_retry = 0;

        while (WiFi.status() != WL_CONNECTED && wifi_retry < 20)
        {
            delay(500);
            Serial.print(".");
            wifi_retry++;
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println(" ✓ Success!");
            Serial.print("  Connected Network: ");
            Serial.println(wifiNetworks[i].ssid);
            Serial.print("  IP: ");
            Serial.println(WiFi.localIP());

            configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
            Serial.println("  ✓ NTP Time Synced");
            wifi_connected = true;
            break;
        }
        else
        {
            Serial.println(" ✗ Failed!");
            WiFi.disconnect();
        }
    }

    if (!wifi_connected)
    {
        Serial.println("\n⚠️  All WiFi Connection Failed - Offline Mode");
    }

    ui_init();

    last_tick = millis();
    last_time_update = millis();
    last_sensor_update = millis();

    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║   System Ready!                       ║");
    Serial.println("║   Sensor Data Every 5 Seconds         ║");
    Serial.println("╚════════════════════════════════════════╝\n");

    delay(1000);
    read_sensor_data();
}

void loop()
{
    uint32_t now = millis();
    lv_tick_inc(now - last_tick);
    last_tick = now;

    if (now - last_time_update >= 1000)
    {
        update_datetime_label();
        last_time_update = now;
    }

    if (now - last_sensor_update >= 5000)
    {
        read_sensor_data();
        last_sensor_update = now;
    }

    Lvgl_Loop();
    delay(5);
}