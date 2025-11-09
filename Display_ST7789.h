#ifndef DISPLAY_ST7789_H
#define DISPLAY_ST7789_H

#include <Arduino.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"

#define LCD_WIDTH  172
#define LCD_HEIGHT 320

#define PIN_NUM_MISO   5
#define PIN_NUM_MOSI   6
#define PIN_NUM_SCLK   7
#define PIN_NUM_CS     14
#define PIN_NUM_DC     15
#define PIN_NUM_RST    21
#define PIN_NUM_BCKL   22

// PWM 설정
#define BACKLIGHT_CHANNEL  0
#define BACKLIGHT_FREQ     5000
#define BACKLIGHT_RES      8  // 8비트 (0-255)

void lcd_init();
void lcd_PushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t *data);
void lcd_fill(uint16_t color);
void lcd_set_brightness(uint8_t brightness);  // 0-100 (퍼센트)

#endif