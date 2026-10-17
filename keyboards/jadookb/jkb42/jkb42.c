
#include "jkb42.h"
#include <qp.h>
#include "qp_comms.h"
#include "qp_st77xx_opcodes.h"
#include "quantum.h"
#include "gfx/jake/jakeoptimized.qgf.h"
#include QMK_KEYBOARD_H

painter_device_t lcd;

void keyboard_post_init_kb(void) {
#if defined(LCD_BLK_PIN) && LCD_BLK_PIN != -1
    gpio_set_pin_output(LCD_BLK_PIN);
    gpio_write_pin_high(LCD_BLK_PIN);
#endif

    wait_ms(LCD_WAIT_TIME);

    // Initialise the LCD
    lcd = qp_st7735_make_spi_device(LCD_HEIGHT, LCD_WIDTH, LCD_CS_PIN, LCD_DC_PIN,
                                  LCD_RST_PIN, LCD_SPI_DIVISOR, 0);
    qp_init(lcd, LCD_ROTATION);

    qp_clear(lcd);

    qp_comms_start(lcd);
    qp_comms_command(lcd, ST77XX_CMD_INVERT_ON);
    qp_comms_stop(lcd);

    // Apply Offset
    qp_set_viewport_offsets(lcd, LCD_OFFSET_X + 1, LCD_OFFSET_Y + 1);

    qp_power(lcd, true);
    qp_flush(lcd);

    qp_rect(lcd, 0, 0, LCD_WIDTH, LCD_HEIGHT, HSV_BLACK, true);
    painter_image_handle_t logo_image = qp_load_image_mem(gfx_jakeoptimized);
    if (logo_image != NULL) {
        qp_drawimage(lcd, 0, 0, logo_image);
    }

    qp_flush(lcd);
    keyboard_post_init_user();
}

void suspend_power_down_user(void) {
    rgb_matrix_set_suspend_state(true);
#if defined(LCD_BLK_PIN) && LCD_BLK_PIN != -1
    gpio_write_pin_low(LCD_BLK_PIN);
#endif
    qp_power(lcd, false);
}

void suspend_wakeup_init_user(void) {
    qp_power(lcd, true);
#if defined(LCD_BLK_PIN) && LCD_BLK_PIN != -1
    gpio_write_pin_high(LCD_BLK_PIN);
#endif
    rgb_matrix_set_suspend_state(false);
}
