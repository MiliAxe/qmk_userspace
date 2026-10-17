
#include "jkb42.h"
#include <qp.h>
#include "qp_comms.h"
#include "qp_st77xx_opcodes.h"
#include "quantum.h"
#include "fonts/thintel15.qff.h"
#include "fonts/robotomono20.qff.h"
#include QMK_KEYBOARD_H

#ifdef WPM_ENABLE
#    include "wpm.h"
#endif

painter_device_t lcd;
static painter_font_handle_t font_sm = NULL;
static painter_font_handle_t font_lg = NULL;

static uint8_t  last_layer    = 255;
static bool     last_caps     = false;
static uint8_t  last_wpm      = 255;
static uint32_t last_wpm_time = 0;

typedef struct {
    const char *name;
    uint8_t     hue;
    uint8_t     sat;
    uint8_t     val;
} layer_info_t;

// Miryoku layer configuration
static const layer_info_t layer_info[] = {
    [0] = { "BASE",   128, 255, 255 }, // Cyan
    [1] = { "EXTRA",  145, 220, 255 }, // Light Blue
    [2] = { "TAP",      0,   0, 255 }, // White
    [3] = { "BUTTON", 106, 255, 255 }, // Teal
    [4] = { "NAV",    191, 255, 255 }, // Purple
    [5] = { "MOUSE",  213, 255, 255 }, // Magenta
    [6] = { "MEDIA",    0, 255, 255 }, // Red
    [7] = { "NUM",     32, 255, 255 }, // Amber / Gold
    [8] = { "SYM",     85, 255, 255 }, // Green
    [9] = { "FUN",     21, 255, 255 }, // Orange
};

static void draw_header(void) {
    // Header title "JKB42"
    qp_drawtext_recolor(lcd, 8, 8, font_sm, "JKB42", 0, 0, 200, 0, 0, 0);

    // Separator line under header
    qp_line(lcd, 6, 26, 121, 26, 0, 0, 60);

    // Separator line above footer
    qp_line(lcd, 6, 84, 121, 84, 0, 0, 60);

    // Footer label "SPEED"
    qp_drawtext_recolor(lcd, 8, 90, font_sm, "SPEED", 0, 0, 150, 0, 0, 0);
}

static void update_caps_indicator(bool caps) {
    if (caps) {
        // Red badge with "CAPS" in white
        qp_rect(lcd, 82, 6, 120, 22, 0, 255, 255, true);
        qp_drawtext_recolor(lcd, 88, 7, font_sm, "CAPS", 0, 0, 255, 0, 255, 255);
    } else {
        // Clear caps area
        qp_rect(lcd, 80, 5, 122, 24, 0, 0, 0, true);
    }
}

static void update_layer_card(uint8_t layer) {
    const layer_info_t *info;
    if (layer < (sizeof(layer_info) / sizeof(layer_info[0]))) {
        info = &layer_info[layer];
    } else {
        static const layer_info_t unknown = { "OTHER", 0, 0, 255 };
        info = &unknown;
    }

    // Clear Layer Card area
    qp_rect(lcd, 6, 32, 121, 80, 0, 0, 0, true);

    // Colored accent border around layer card
    qp_rect(lcd, 6, 32, 121, 80, info->hue, info->sat, info->val, false);
    qp_rect(lcd, 7, 33, 120, 79, info->hue, info->sat, info->val, false);

    // Layer badge label inside card (e.g. "LAYER 4")
    char layer_num_str[16];
    snprintf(layer_num_str, sizeof(layer_num_str), "LAYER %u", (unsigned int)layer);
    int16_t num_w = qp_textwidth(font_sm, layer_num_str);
    int16_t num_x = (128 - num_w) / 2;
    if (num_x < 8) num_x = 8;
    qp_drawtext_recolor(lcd, num_x, 38, font_sm, layer_num_str, 0, 0, 180, 0, 0, 0);

    // Big Layer Name in center of card (e.g. "NAV")
    int16_t text_w = qp_textwidth(font_lg, info->name);
    int16_t text_x = (128 - text_w) / 2;
    if (text_x < 8) text_x = 8;
    qp_drawtext_recolor(lcd, text_x, 54, font_lg, info->name, info->hue, info->sat, info->val, 0, 0, 0);
}

#ifdef WPM_ENABLE
static void update_wpm_gauge(uint8_t wpm) {
    // Format WPM string
    char wpm_str[16];
    snprintf(wpm_str, sizeof(wpm_str), "%3u WPM", (unsigned int)wpm);
    qp_drawtext_recolor(lcd, 72, 90, font_sm, wpm_str, 0, 0, 255, 0, 0, 0);

    const uint16_t bar_x0  = 8;
    const uint16_t bar_y0  = 108;
    const uint16_t bar_x1  = 119;
    const uint16_t bar_y1  = 118;
    const uint16_t max_wpm = 120;

    // Progress bar frame
    qp_rect(lcd, bar_x0, bar_y0, bar_x1, bar_y1, 0, 0, 80, false);

    // Fill width
    uint16_t fill_w = ((uint32_t)MIN(wpm, max_wpm) * (bar_x1 - bar_x0 - 2)) / max_wpm;
    uint8_t  hue;
    if (wpm < 30) {
        hue = 85;  // Green
    } else if (wpm < 60) {
        hue = 45;  // Yellow
    } else if (wpm < 90) {
        hue = 21;  // Orange
    } else {
        hue = 0;   // Red
    }

    if (fill_w > 0) {
        qp_rect(lcd, bar_x0 + 1, bar_y0 + 1, bar_x0 + 1 + fill_w, bar_y1 - 1, hue, 255, 255, true);
    }
    // Clear the rest of the trough
    if (bar_x0 + 1 + fill_w < bar_x1 - 1) {
        qp_rect(lcd, bar_x0 + 1 + fill_w, bar_y0 + 1, bar_x1 - 1, bar_y1 - 1, 0, 0, 20, true);
    }
}
#endif

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

    qp_set_viewport_offsets(lcd, LCD_OFFSET_X + 1, LCD_OFFSET_Y + 1);

    qp_power(lcd, true);

    // Load fonts
    font_sm = qp_load_font_mem(font_thintel15);
    font_lg = qp_load_font_mem(font_robotomono20);

    // Clear background to black
    qp_rect(lcd, 0, 0, LCD_WIDTH, LCD_HEIGHT, 0, 0, 0, true);

    // Initial draw
    draw_header();
    update_caps_indicator(host_keyboard_led_state().caps_lock);
    update_layer_card(get_highest_layer(layer_state));
#ifdef WPM_ENABLE
    update_wpm_gauge(get_current_wpm());
#endif
    qp_flush(lcd);

    keyboard_post_init_user();
}

void housekeeping_task_kb(void) {
    if (lcd == NULL || font_sm == NULL) {
        return;
    }

    bool need_flush = false;

    // 1. Layer update (instant response on thumb key press)
    uint8_t current_layer = get_highest_layer(layer_state);
    if (current_layer != last_layer) {
        last_layer = current_layer;
        update_layer_card(current_layer);
        need_flush = true;
    }

    // 2. Caps lock update
    bool current_caps = host_keyboard_led_state().caps_lock;
    if (current_caps != last_caps) {
        last_caps = current_caps;
        update_caps_indicator(current_caps);
        need_flush = true;
    }

#ifdef WPM_ENABLE
    // 3. WPM update (throttled to at most once per 100ms)
    if (timer_elapsed32(last_wpm_time) >= 100) {
        last_wpm_time = timer_read32();
        uint8_t current_wpm = get_current_wpm();
        if (current_wpm != last_wpm) {
            last_wpm = current_wpm;
            update_wpm_gauge(current_wpm);
            need_flush = true;
        }
    }
#endif

    if (need_flush) {
        qp_flush(lcd);
    }
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
