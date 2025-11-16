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
    {"Stonei", "Dsji0201!@"}
};
const int numNetworks = 2;

// NTP Server Configuration
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 32400;
const int daylightOffset_sec = 0;

// I2C Pin Definition
#define I2C_SDA 0
#define I2C_SCL 1

// Temperature Calibration
#define TEMP_CALIBRATION_OFFSET -4.0f

// Sensors
Adafruit_AHTX0 aht;
ScioSense_ENS160 ens160(ENS160_I2CADDR_1); // 0x52 address

static uint32_t last_tick = 0;
static uint32_t last_time_update = 0;
static uint32_t last_sensor_update = 0;

bool aht_available = false;
bool ens160_available = false;
uint32_t ens160_warmup_start = 0;

// Weekday String Array
const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// Update DateTime Label Function
void update_datetime_label()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        return;
    }

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
    Serial.println("\n=== I2C Device Scan ===");
    byte count = 0;

    for (byte address = 1; address < 127; address++)
    {
        Wire.beginTransmission(address);
        byte error = Wire.endTransmission();

        if (error == 0)
        {
            Serial.print("Found: 0x");
            if (address < 16) Serial.print("0");
            Serial.print(address, HEX);

            if (address == 0x38) Serial.print(" (AHT21)");
            if (address == 0x52) Serial.print(" (ENS160)");
            if (address == 0x53) Serial.print(" (ENS160)");

            Serial.println();
            count++;
        }
        delay(1); // 안정성을 위한 작은 지연
    }

    Serial.print("Total: ");
    Serial.print(count);
    Serial.println(" device(s)\n");
}

// Magnus 공식을 이용한 습도 보정 함수
float compensate_humidity(float measured_temp, float measured_humidity, float actual_temp)
{
    // Magnus 공식 계수
    const float a = 17.27;
    const float b = 237.7;
    
    // 측정 온도에서의 포화 수증기압 계산
    float gamma_measured = (a * measured_temp) / (b + measured_temp);
    float saturation_measured = exp(gamma_measured);
    
    // 실제 온도에서의 포화 수증기압 계산
    float gamma_actual = (a * actual_temp) / (b + actual_temp);
    float saturation_actual = exp(gamma_actual);
    
    // 절대 수증기압은 동일하므로 상대습도 재계산
    float compensated_humidity = measured_humidity * (saturation_measured / saturation_actual);
    
    // 0-100% 범위로 제한
    if (compensated_humidity < 0) compensated_humidity = 0;
    if (compensated_humidity > 100) compensated_humidity = 100;
    
    return compensated_humidity;
}

// AQI Level
const char *getAQILevel(uint8_t aqi)
{
    switch(aqi)
    {
        case 1: return "Excellent";
        case 2: return "Good";
        case 3: return "Moderate";
        case 4: return "Poor";
        case 5: return "Unhealthy";
        default: return "Measuring";
    }
}

// Read Sensor Data
void read_sensor_data()
{
    Serial.println("\n--- Sensor Reading ---");

    float temp = 0, humid = 0;

    // AHT21 Reading
    if (aht_available)
    {
        sensors_event_t humidity, temperature;
        aht.getEvent(&humidity, &temperature);
        
        float raw_temp = temperature.temperature;
        float raw_humid = humidity.relative_humidity;
        
        // 온도 보정
        temp = raw_temp + TEMP_CALIBRATION_OFFSET;
        
        // 습도 보정 (온도 변화에 따른)
        humid = compensate_humidity(raw_temp, raw_humid, temp);

        Serial.print("Temperature: ");
        Serial.print(temp, 1);
        Serial.print("°C (raw: ");
        Serial.print(raw_temp, 1);
        Serial.println("°C)");
        
        Serial.print("Humidity: ");
        Serial.print(humid, 1);
        Serial.print("% (raw: ");
        Serial.print(raw_humid, 1);
        Serial.println("%)");

        // UI Update - Temperature
        if (ui_temperaturevalue)
        {
            char temp_buf[12];
            snprintf(temp_buf, sizeof(temp_buf), "%.1fC", temp);
            lv_label_set_text(ui_temperaturevalue, temp_buf);
        }

        // UI Update - Humidity
        if (ui_humitygraph && ui_humityvalue)
        {
            int humid_int = (int)(humid + 0.5f);
            if (humid_int < 0) humid_int = 0;
            if (humid_int > 100) humid_int = 100;
            
            lv_arc_set_value(ui_humitygraph, humid_int);
            
            char humid_buf[8];
            snprintf(humid_buf, sizeof(humid_buf), "%.1f%%", humid);
            lv_label_set_text(ui_humityvalue, humid_buf);
        }
    }

    // ENS160 Reading - available() 체크 없이 무조건 시도
    if (ens160_available)
    {
        uint32_t elapsed = (millis() - ens160_warmup_start) / 1000;
        Serial.print("ENS160 Elapsed: ");
        Serial.print(elapsed);
        Serial.print("s");
        
        if (elapsed < 180)
        {
            Serial.print(" (Warmup: ");
            Serial.print((elapsed * 100) / 180);
            Serial.print("%)");
        }
        Serial.println();

        // 온습도 보정 먼저 수행 (중요!) - 보정된 값 사용
        if (aht_available && temp > -50)  // 유효한 온도 값인지 확인
        {
            uint16_t t_ens210 = (uint16_t)((temp + 273.15) * 64);
            uint16_t h_ens210 = (uint16_t)(humid * 512);
            ens160.set_envdata210(t_ens210, h_ens210);
            
            Serial.print("  Compensation -> T:");
            Serial.print(t_ens210);
            Serial.print(", H:");
            Serial.println(h_ens210);
        }

        // 측정 수행
        Serial.print("  Measuring... ");
        ens160.measure(false);
        delay(100); // 측정 완료 대기 시간 증가
        Serial.println("done");

        // available() 체크 없이 바로 값 읽기 시도
        Serial.println("  Reading values:");
        
        uint8_t aqi = ens160.getAQI();
        uint16_t tvoc = ens160.getTVOC();
        uint16_t eco2 = ens160.geteCO2();

        Serial.print("    AQI: ");
        Serial.print(aqi);
        Serial.print(" (");
        Serial.print(getAQILevel(aqi));
        Serial.println(")");

        Serial.print("    TVOC: ");
        Serial.print(tvoc);
        Serial.println(" ppb");

        Serial.print("    eCO2: ");
        Serial.print(eco2);
        Serial.println(" ppm");

        // UI 업데이트 - 0이어도 표시
        if (ui_aqivalue)
        {
            char aqi_buf[16];
            if (aqi == 0)
                snprintf(aqi_buf, sizeof(aqi_buf), "0*");
            else
                snprintf(aqi_buf, sizeof(aqi_buf), "%u", (unsigned int)aqi);
            lv_label_set_text(ui_aqivalue, aqi_buf);
        }

        // UI Update - CO2
        if (ui_co2value)
        {
            char co2_buf[16];
            if (eco2 == 0)
                snprintf(co2_buf, sizeof(co2_buf), "0*");
            else
                snprintf(co2_buf, sizeof(co2_buf), "%u", (unsigned int)eco2);
            lv_label_set_text(ui_co2value, co2_buf);
        }

        // 워밍업 상태 안내
        if (elapsed < 180)
        {
            Serial.println("  * Values marked with * may be inaccurate (warmup)");
        }
    }

    Serial.println("--- End ---\n");
}

void setup()
{
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n\n========================================");
    Serial.println("  ESP32-C6 Sensor System Starting");
    Serial.println("========================================\n");

    // I2C Initialization
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000); // 100kHz for stability
    Serial.println("I2C: Initialized (100kHz)");
    delay(200);

    scan_i2c_devices();

    // AHT21 Initialization
    Serial.print("AHT21: Initializing... ");
    delay(100);
    if (aht.begin())
    {
        aht_available = true;
        Serial.println("OK");
    }
    else
    {
        aht_available = false;
        Serial.println("FAILED");
    }

    // ENS160 Initialization - 더 강제적인 초기화
    Serial.println("\n--- ENS160 Initialization ---");
    
    delay(100);
    Serial.print("Calling begin()... ");
    bool init_result = ens160.begin();
    Serial.println(init_result ? "OK" : "FAILED");
    
    // begin()이 실패해도 계속 시도
    if (!init_result)
    {
        Serial.println("Retrying with reset...");
        delay(500);
        ens160.setMode(ENS160_OPMODE_RESET); // 리셋 시도
        delay(1000);
        init_result = ens160.begin();
        Serial.print("Retry result: ");
        Serial.println(init_result ? "OK" : "FAILED");
    }
    
    delay(200);

    // 펌웨어 버전 읽기 (센서 통신 확인)
    Serial.print("Firmware: ");
    Serial.print(ens160.getMajorRev());
    Serial.print(".");
    Serial.print(ens160.getMinorRev());
    Serial.print(".");
    Serial.println(ens160.getBuild());

    delay(100);
    
    // 모드 설정
    Serial.print("Setting IDLE mode... ");
    ens160.setMode(ENS160_OPMODE_IDLE);
    delay(200);
    Serial.println("OK");
    
    Serial.print("Setting STANDARD mode... ");
    bool mode_set = ens160.setMode(ENS160_OPMODE_STD);
    delay(200);
    Serial.println(mode_set ? "OK" : "FAILED");

    // 무조건 사용 가능하도록 설정
    ens160_available = true;
    ens160_warmup_start = millis();
    
    Serial.println("ENS160: Enabled (reading all values)");
    Serial.println("Note: Initial values may be 0 during warmup");

    Serial.println();

    // LCD Initialization
    Serial.print("LCD: Initializing... ");
    Lvgl_Init();
    lcd_set_brightness(10);
    Serial.println("OK");

    // WiFi Connection - 간소화
    Serial.println("\n--- WiFi Connection ---");
    bool wifi_connected = false;

    for (int i = 0; i < numNetworks; i++)
    {
        Serial.print("Trying: ");
        Serial.print(wifiNetworks[i].ssid);
        Serial.print("... ");

        WiFi.begin(wifiNetworks[i].ssid, wifiNetworks[i].password);
        int retry = 0;

        while (WiFi.status() != WL_CONNECTED && retry < 15)
        {
            delay(500);
            Serial.print(".");
            retry++;
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println(" Connected!");
            Serial.print("IP: ");
            Serial.println(WiFi.localIP());

            configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
            wifi_connected = true;
            break;
        }
        else
        {
            Serial.println(" Failed");
            WiFi.disconnect();
            delay(100);
        }
    }

    if (!wifi_connected)
    {
        Serial.println("WiFi: Offline mode");
    }

    Serial.println();

    // UI Initialization
    ui_init();

    last_tick = millis();
    last_time_update = millis();
    last_sensor_update = millis();

    Serial.println("========================================");
    Serial.println("  System Ready!");
    Serial.println("  Sensors update every 5 seconds");
    Serial.print("  Temperature calibration: ");
    Serial.print(TEMP_CALIBRATION_OFFSET);
    Serial.println("°C");
    Serial.println("========================================\n");

    delay(500);
    read_sensor_data();
}

void loop()
{
    uint32_t now = millis();
    
    // LVGL tick
    lv_tick_inc(now - last_tick);
    last_tick = now;

    // Time update (every 1 second)
    if (now - last_time_update >= 1000)
    {
        update_datetime_label();
        last_time_update = now;
    }

    // Sensor update (every 5 seconds)
    if (now - last_sensor_update >= 5000)
    {
        read_sensor_data();
        last_sensor_update = now;
    }

    // LVGL handler
    Lvgl_Loop();
    
    // Watchdog reset 방지
    yield();
    delay(5);
}