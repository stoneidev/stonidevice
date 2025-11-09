#include "LVGL_Driver.h"

#define LVGL_BUF_SIZE (LCD_HEIGHT * LCD_WIDTH / 10)  // 버퍼 크기

static lv_display_t *disp;
static lv_color_t buf1[LVGL_BUF_SIZE];
static lv_color_t buf2[LVGL_BUF_SIZE];

// Display flush callback (LVGL 9.x)
void my_disp_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    
    // ST7789에 데이터 전송
    lcd_PushColors(area->x1, area->y1, w, h, (uint16_t *)px_map);
    
    // LVGL에 flush 완료 알림
    lv_display_flush_ready(display);
}

// Touchpad read callback (LVGL 9.x) - 터치 스크린이 있는 경우
void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    // 터치 스크린이 없다면 이 함수는 필요 없습니다
    // Waveshare ESP32-C6-LCD-1.47은 터치 스크린이 없으므로 
    // 이 함수는 비워둡니다
    data->state = LV_INDEV_STATE_RELEASED;
}

void Lvgl_Init()
{
    // LCD 하드웨어 초기화
    lcd_init();
    
    // LVGL 초기화
    lv_init();
    
    // Display 생성 (LVGL 9.x)
    disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    
    // Display 버퍼 설정
    lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    
    // Flush callback 설정
    lv_display_set_flush_cb(disp, my_disp_flush);
    
    // 필요시 회전 설정
    // lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
}

void Lvgl_Loop()
{
    lv_timer_handler();  // LVGL 9.x에서는 lv_task_handler 대신 lv_timer_handler 사용
}