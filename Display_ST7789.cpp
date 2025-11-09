#include "Display_ST7789.h"

static spi_device_handle_t spi_handle;

// ST7789 명령어
#define ST7789_SWRESET 0x01
#define ST7789_SLPOUT  0x11
#define ST7789_NORON   0x13
#define ST7789_INVON   0x21
#define ST7789_DISPON  0x29
#define ST7789_CASET   0x2A
#define ST7789_RASET   0x2B
#define ST7789_RAMWR   0x2C
#define ST7789_MADCTL  0x36
#define ST7789_COLMOD  0x3A

// Offset 설정
#define LCD_X_OFFSET 34  // (240 - 172) / 2 = 34
#define LCD_Y_OFFSET 0

void lcd_cmd(const uint8_t cmd)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &cmd;
    t.user = (void*)0; // DC=0 for command
    spi_device_polling_transmit(spi_handle, &t);
}

void lcd_data(const uint8_t *data, int len)
{
    spi_transaction_t t;
    if (len == 0) return;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    t.user = (void*)1; // DC=1 for data
    spi_device_polling_transmit(spi_handle, &t);
}

void lcd_spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc = (int)t->user;
    gpio_set_level((gpio_num_t)PIN_NUM_DC, dc);
}

void lcd_init()
{
    Serial.println("=== LCD Init Start ===");
    
    // GPIO 초기화 (백라이트 제외)
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << PIN_NUM_DC) | (1ULL << PIN_NUM_RST);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    
    Serial.println("Step 1: GPIO configured");
    
    // SPI 버스 초기화
    spi_bus_config_t buscfg = {};
    buscfg.miso_io_num = PIN_NUM_MISO;
    buscfg.mosi_io_num = PIN_NUM_MOSI;
    buscfg.sclk_io_num = PIN_NUM_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2 + 8;
    
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        Serial.printf("SPI bus init failed: %d\n", ret);
        return;
    }
    Serial.println("Step 2: SPI bus initialized");
    
    // SPI 디바이스 추가
    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = 80 * 1000 * 1000;
    devcfg.mode = 0;
    devcfg.spics_io_num = PIN_NUM_CS;
    devcfg.queue_size = 7;
    devcfg.pre_cb = lcd_spi_pre_transfer_callback;
    
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        Serial.printf("SPI device add failed: %d\n", ret);
        return;
    }
    Serial.println("Step 3: SPI device added");
    
    // 하드웨어 리셋
    gpio_set_level((gpio_num_t)PIN_NUM_RST, 0);
    delay(100);
    gpio_set_level((gpio_num_t)PIN_NUM_RST, 1);
    delay(100);
    Serial.println("Step 4: Hardware reset done");
    
    // LCD 초기화 시퀀스
    lcd_cmd(ST7789_SWRESET);
    delay(120);
    Serial.println("Step 5: Software reset");
    
    lcd_cmd(ST7789_SLPOUT);
    delay(120);
    Serial.println("Step 6: Sleep out");
    
    lcd_cmd(ST7789_COLMOD);
    uint8_t data1 = 0x55;
    lcd_data(&data1, 1);
    delay(10);
    Serial.println("Step 7: Color mode set");
    
    lcd_cmd(ST7789_MADCTL);
    uint8_t data2 = 0x00;
    lcd_data(&data2, 1);
    delay(10);
    Serial.println("Step 8: MADCTL set");
    
    lcd_cmd(ST7789_INVON);
    delay(10);
    Serial.println("Step 9: Inversion on");
    
    lcd_cmd(ST7789_NORON);
    delay(10);
    Serial.println("Step 10: Normal mode");
    
    lcd_cmd(ST7789_DISPON);
    delay(100);
    Serial.println("Step 11: Display on");
    
    // Arduino ESP32 3.x 스타일 PWM 설정
    ledcAttach(PIN_NUM_BCKL, BACKLIGHT_FREQ, BACKLIGHT_RES);
    
    // 초기 밝기 80% 설정
    lcd_set_brightness(80);
    Serial.println("Step 12: Backlight PWM configured (80%)");
    
    Serial.println("=== LCD Init Complete ===");
}

// 밝기 제어 함수 (0-100%)
void lcd_set_brightness(uint8_t brightness)
{
    if (brightness > 100) brightness = 100;
    
    // 0-100%를 0-255로 변환
    uint32_t duty = (brightness * 255) / 100;
    
    ledcWrite(PIN_NUM_BCKL, duty);
    
    Serial.printf("Brightness set to %d%% (duty: %d)\n", brightness, duty);
}

void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    // Offset 적용
    x0 += LCD_X_OFFSET;
    x1 += LCD_X_OFFSET;
    y0 += LCD_Y_OFFSET;
    y1 += LCD_Y_OFFSET;
    
    lcd_cmd(ST7789_CASET);
    uint8_t xdata[] = {
        (uint8_t)(x0 >> 8), 
        (uint8_t)(x0 & 0xFF),
        (uint8_t)(x1 >> 8), 
        (uint8_t)(x1 & 0xFF)
    };
    lcd_data(xdata, 4);
    
    lcd_cmd(ST7789_RASET);
    uint8_t ydata[] = {
        (uint8_t)(y0 >> 8), 
        (uint8_t)(y0 & 0xFF),
        (uint8_t)(y1 >> 8), 
        (uint8_t)(y1 & 0xFF)
    };
    lcd_data(ydata, 4);
    
    lcd_cmd(ST7789_RAMWR);
}

void lcd_fill(uint16_t color)
{
    Serial.printf("Filling screen with color: 0x%04X\n", color);
    
    lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    
    uint16_t *line = (uint16_t*)heap_caps_malloc(LCD_WIDTH * 2, MALLOC_CAP_DMA);
    if (!line) {
        Serial.println("Failed to allocate memory!");
        return;
    }
    
    for (int i = 0; i < LCD_WIDTH; i++) {
        line[i] = (color >> 8) | (color << 8); // Swap bytes for RGB565
    }
    
    for (int y = 0; y < LCD_HEIGHT; y++) {
        lcd_data((uint8_t*)line, LCD_WIDTH * 2);
    }
    
    free(line);
    Serial.println("Fill complete");
}

void lcd_PushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t *data)
{
    lcd_set_window(x, y, x + width - 1, y + height - 1);
    
    // Byte swap
    uint32_t len = width * height;
    for (uint32_t i = 0; i < len; i++) {
        data[i] = (data[i] >> 8) | (data[i] << 8);
    }
    
    lcd_data((uint8_t*)data, len * 2);
}