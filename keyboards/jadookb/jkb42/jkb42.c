
#include "jkb42.h"
#include <qp.h>
#include "qp_comms.h"
#include "qp_st77xx_opcodes.h"
#include "quantum.h"
#include "print.h"
#include "fonts/thintel15.qff.h"
#include "fonts/robotomono20.qff.h"
#include QMK_KEYBOARD_H

#ifdef WPM_ENABLE
#    include "wpm.h"
#endif

#if defined(CAPS_WORD_ENABLE)
#    include "caps_word.h"
#endif

painter_device_t lcd;
static painter_font_handle_t font_sm = NULL;
static painter_font_handle_t font_lg = NULL;

static uint8_t  last_layer      = 255;
static uint8_t  last_lock_state = 255; // 0: None, 1: Caps Word, 2: Caps Lock
static uint8_t  last_mods       = 255;
static uint8_t  last_wpm        = 255;
static uint32_t last_wpm_time   = 0;
static bool     display_stealth_mode = false;

typedef struct {
    const char *name;
    uint8_t     hue;
    uint8_t     sat;
    uint8_t     val;
} layer_info_t;

// Miryoku layer configuration
static const layer_info_t layer_info[] = {
    [0] = { "BASE",    43, 255, 255 }, // Yellow
    [1] = { "EXTRA",  145, 220, 255 }, // Light Blue
    [2] = { "TAP",      0,   0, 255 }, // White
    [3] = { "BUTTON", 106, 255, 255 }, // Teal
    [4] = { "NAV",    191, 255, 255 }, // Purple
    [5] = { "MOUSE",  213, 255, 255 }, // Magenta
    [6] = { "MEDIA",    0, 255, 255 }, // Red
    [7] = { "NUM",    128, 255, 255 }, // Cyan
    [8] = { "SYM",     85, 255, 255 }, // Green
    [9] = { "FUN",     21, 255, 255 }, // Orange
};

typedef struct {
    const char *label;
    uint8_t     mask;
    uint16_t    x0;
    uint16_t    x1;
    uint8_t     hue;
} mod_pill_t;

static const mod_pill_t mod_pills[4] = {
    { "SFT", MOD_MASK_SHIFT,  6,  32,  32 }, // Amber Gold
    { "CTL", MOD_MASK_CTRL,  36,  62, 128 }, // Cyan
    { "ALT", MOD_MASK_ALT,   66,  92, 195 }, // Purple / Magenta
    { "GUI", MOD_MASK_GUI,   96, 122,  85 }, // Green
};

#ifndef DISPLAY_HEADER_TEXT
#    define DISPLAY_HEADER_TEXT "Mili's KB"
#endif

static void draw_header(void) {
    // Header title
    qp_drawtext_recolor(lcd, 8, 6, font_sm, DISPLAY_HEADER_TEXT, 0, 0, 200, 0, 0, 0);

    // Separator line under header
    qp_line(lcd, 6, 24, 121, 24, 0, 0, 60);

    // Separator line above footer
    qp_line(lcd, 6, 94, 121, 94, 0, 0, 60);

    // Footer label "SPEED"
    qp_drawtext_recolor(lcd, 8, 98, font_sm, "SPEED", 0, 0, 150, 0, 0, 0);
}

static void update_lock_indicator(uint8_t lock_state) {
    if (lock_state == 2) {
        // Red badge with "CAPS" in white
        qp_rect(lcd, 82, 5, 122, 21, 0, 255, 255, true);
        qp_drawtext_recolor(lcd, 88, 6, font_sm, "CAPS", 0, 0, 255, 0, 255, 255);
    } else if (lock_state == 1) {
        // Amber badge with "WORD" in black
        qp_rect(lcd, 82, 5, 122, 21, 32, 255, 255, true);
        qp_drawtext_recolor(lcd, 88, 6, font_sm, "WORD", 0, 0, 0, 32, 255, 255);
    } else {
        // Clear lock indicator area
        qp_rect(lcd, 80, 5, 122, 22, 0, 0, 0, true);
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

    // Clear Layer Card area (height 45px)
    qp_rect(lcd, 6, 28, 121, 72, 0, 0, 0, true);

    // Colored accent border around layer card
    qp_rect(lcd, 6, 28, 121, 72, info->hue, info->sat, info->val, false);
    qp_rect(lcd, 7, 29, 120, 71, info->hue, info->sat, info->val, false);

    // Layer badge label inside card (e.g. "LAYER 4")
    char layer_num_str[16];
    snprintf(layer_num_str, sizeof(layer_num_str), "LAYER %u", (unsigned int)layer);
    int16_t num_w = qp_textwidth(font_sm, layer_num_str);
    if (num_w <= 0) num_w = 40;
    int16_t num_x = (128 - num_w) / 2;
    if (num_x < 8) num_x = 8;
    qp_drawtext_recolor(lcd, num_x, 32, font_sm, layer_num_str, 0, 0, 180, 0, 0, 0);

    // Big Layer Name in center of card (e.g. "NAV")
    painter_font_handle_t title_font = font_lg ? font_lg : font_sm;
    int16_t text_w = qp_textwidth(title_font, info->name);
    if (text_w <= 0) text_w = 40;
    int16_t text_x = (128 - text_w) / 2;
    if (text_x < 8) text_x = 8;
    qp_drawtext_recolor(lcd, text_x, 48, title_font, info->name, info->hue, info->sat, info->val, 0, 0, 0);
}

static void update_modifier_pills(uint8_t mods) {
    const uint16_t y0 = 76;
    const uint16_t y1 = 90;

    for (uint8_t i = 0; i < 4; i++) {
        const mod_pill_t *p = &mod_pills[i];
        bool is_active = (mods & p->mask) != 0;

        int16_t tw = qp_textwidth(font_sm, p->label);
        if (tw <= 0) tw = 15;
        int16_t tx = p->x0 + ((p->x1 - p->x0 + 1) - tw) / 2;
        if (tx < p->x0 + 1) tx = p->x0 + 1;

        if (is_active) {
            // Active: Solid colored pill with black text
            qp_rect(lcd, p->x0, y0, p->x1, y1, p->hue, 255, 255, true);
            qp_drawtext_recolor(lcd, tx, y0 + 1, font_sm, p->label, 0, 0, 0, p->hue, 255, 255);
        } else {
            // Inactive: Dark frame, dim text on black
            qp_rect(lcd, p->x0, y0, p->x1, y1, 0, 0, 50, false);
            qp_rect(lcd, p->x0 + 1, y0 + 1, p->x1 - 1, y1 - 1, 0, 0, 0, true);
            qp_drawtext_recolor(lcd, tx, y0 + 1, font_sm, p->label, 0, 0, 90, 0, 0, 0);
        }
    }
}

#ifdef WPM_ENABLE
static void update_wpm_gauge(uint8_t wpm) {
    // Format WPM string
    char wpm_str[16];
    snprintf(wpm_str, sizeof(wpm_str), "%3u WPM", (unsigned int)wpm);
    qp_drawtext_recolor(lcd, 72, 98, font_sm, wpm_str, 0, 0, 255, 0, 0, 0);

    const uint16_t bar_x0  = 8;
    const uint16_t bar_y0  = 113;
    const uint16_t bar_x1  = 119;
    const uint16_t bar_y1  = 123;
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

    last_lock_state = 0;
    if (host_keyboard_led_state().caps_lock) {
        last_lock_state = 2;
    }
#if defined(CAPS_WORD_ENABLE)
    else if (is_caps_word_on()) {
        last_lock_state = 1;
    }
#endif
    update_lock_indicator(last_lock_state);

    last_layer = get_highest_layer(layer_state | default_layer_state);
    update_layer_card(last_layer);

    last_mods = get_mods() | get_weak_mods();
#ifndef NO_ACTION_ONESHOT
    last_mods |= get_oneshot_mods();
#endif
    update_modifier_pills(last_mods);

#ifdef WPM_ENABLE
    last_wpm = get_current_wpm();
    update_wpm_gauge(last_wpm);
#endif
    qp_flush(lcd);

    keyboard_post_init_user();
}

layer_state_t layer_state_set_kb(layer_state_t state) {
    uint8_t next_layer = get_highest_layer(state | default_layer_state);
    if (!display_stealth_mode && lcd != NULL && font_sm != NULL && next_layer != last_layer) {
        last_layer = next_layer;
        update_layer_card(next_layer);
        qp_flush(lcd);
    }
    return layer_state_set_user(state);
}

layer_state_t default_layer_state_set_kb(layer_state_t state) {
    uint8_t next_layer = get_highest_layer(layer_state | state);
    if (!display_stealth_mode && lcd != NULL && font_sm != NULL && next_layer != last_layer) {
        last_layer = next_layer;
        update_layer_card(next_layer);
        qp_flush(lcd);
    }
    return default_layer_state_set_user(state);
}

#if defined(CAPS_WORD_ENABLE)
void caps_word_set_user(bool active) {
    uint8_t current_lock = 0;
    if (host_keyboard_led_state().caps_lock) {
        current_lock = 2;
    } else if (active) {
        current_lock = 1;
    }
    if (!display_stealth_mode && lcd != NULL && font_sm != NULL && current_lock != last_lock_state) {
        last_lock_state = current_lock;
        update_lock_indicator(current_lock);
        qp_flush(lcd);
    }
}
#endif

bool is_display_stealth_mode(void) {
    return display_stealth_mode;
}

void display_stealth_toggle(void) {
    display_stealth_mode = !display_stealth_mode;
    uprintf("TOGGLE STEALTH: new_state=%d\n", display_stealth_mode);
    if (display_stealth_mode) {
        // 1. Physically paint the entire screen black
        qp_rect(lcd, 0, 0, 127, 127, 0, 0, 0, true);
        qp_flush(lcd);

        // 2. Put ST7735 into sleep mode
        qp_comms_start(lcd);
        qp_comms_command(lcd, ST77XX_CMD_DISPLAY_OFF);
        qp_comms_command(lcd, ST77XX_CMD_SLEEP_ON);
        qp_comms_stop(lcd);

        // 3. Cut backlight
#if defined(LCD_BLK_PIN) && LCD_BLK_PIN != -1
        gpio_write_pin_low(LCD_BLK_PIN);
#endif
    } else {
        // Wake up ST7735
        qp_comms_start(lcd);
        qp_comms_command(lcd, ST77XX_CMD_SLEEP_OFF);
        qp_comms_command(lcd, ST77XX_CMD_DISPLAY_ON);
        qp_comms_stop(lcd);

#if defined(LCD_BLK_PIN) && LCD_BLK_PIN != -1
        gpio_write_pin_high(LCD_BLK_PIN);
#endif
        draw_header();
        last_layer      = 255;
        last_lock_state = 255;
        last_mods       = 255;
        last_wpm        = 255;
        update_layer_card(get_highest_layer(layer_state | default_layer_state));
        qp_flush(lcd);
    }
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    uprintf("KEY: pr=%d r=%d c=%d kc=0x%04x L=%d\n",
            record->event.pressed,
            record->event.key.row,
            record->event.key.col,
            keycode,
            get_highest_layer(layer_state));

    // Display stealth toggle strictly on OU_AUTO (slash key on Media layer)
    if (keycode == OU_AUTO) {
        if (record->event.pressed) {
            display_stealth_toggle();
        }
        return false;
    }

    if (keycode == CW_TOGG) {
        if (record->event.pressed) {
            uint8_t mods = get_mods() | get_weak_mods();
#ifndef NO_ACTION_ONESHOT
            mods |= get_oneshot_mods();
#endif
            static uint16_t last_cw_time = 0;
            bool is_double_tap = (timer_elapsed(last_cw_time) < 350);
            last_cw_time = timer_read();

            if ((mods & MOD_MASK_SHIFT) || is_double_tap) {
                // Shift held OR double-tapped -> Toggle Full Caps Lock!
                uint8_t real_mods = get_mods();
                del_mods(MOD_MASK_SHIFT);
                send_keyboard_report();
                tap_code(KC_CAPS);
                set_mods(real_mods);
                send_keyboard_report();
#if defined(CAPS_WORD_ENABLE)
                if (is_caps_word_on()) {
                    caps_word_off();
                }
#endif
                return false;
            }
        }
    }
    return process_record_user(keycode, record);
}

void housekeeping_task_kb(void) {
    if (lcd == NULL || font_sm == NULL || display_stealth_mode) {
        return;
    }

#if defined(QUANTUM_PAINTER_DISPLAY_TIMEOUT) && (QUANTUM_PAINTER_DISPLAY_TIMEOUT > 0)
    uint32_t idle_time = last_input_activity_elapsed();
    static bool stage1_dimmed = false;
    static bool stage2_off    = false;

#    if defined(DISPLAY_OFF_TIMEOUT) && (DISPLAY_OFF_TIMEOUT > 0)
    // Stage 2: Deep Sleep (backlight completely cuts off after long timeout)
    if (idle_time >= DISPLAY_OFF_TIMEOUT) {
        if (!stage2_off) {
            stage2_off = true;
#        if defined(LCD_BLK_PIN) && LCD_BLK_PIN != -1
            gpio_write_pin_low(LCD_BLK_PIN);
#        endif
        }
        return;
    }

    // Waking up from Stage 2
    if (stage2_off) {
        stage2_off = false;
#        if defined(LCD_BLK_PIN) && LCD_BLK_PIN != -1
            gpio_write_pin_high(LCD_BLK_PIN);
#        endif
        stage1_dimmed = true; // force full repaint on wake
    }
#    endif

    // Stage 1: Soft Standby (screen pixels sleep, ambient backlight remains on)
    if (idle_time >= QUANTUM_PAINTER_DISPLAY_TIMEOUT) {
        stage1_dimmed = true;
        return; // qp_internal already puts ST7735 controller in low-power sleep
    }

    // Waking up from Stage 1
    if (stage1_dimmed) {
        stage1_dimmed = false;
        // Invalidate cached states to force full screen repaint
        draw_header();
        last_layer      = 255;
        last_lock_state = 255;
        last_mods       = 255;
        last_wpm        = 255;
    }
#endif

    bool need_flush = false;

    // 1. Layer update (instant response on thumb key press / split sync on slave)
    uint8_t current_layer = get_highest_layer(layer_state | default_layer_state);
    if (current_layer != last_layer) {
        last_layer = current_layer;
        update_layer_card(current_layer);
        need_flush = true;
    }

    // 2. Lock state update (Caps Lock / Caps Word)
    uint8_t current_lock = 0;
    if (host_keyboard_led_state().caps_lock) {
        current_lock = 2;
    }
#if defined(CAPS_WORD_ENABLE)
    else if (is_caps_word_on()) {
        current_lock = 1;
    }
#endif
    if (current_lock != last_lock_state) {
        last_lock_state = current_lock;
        update_lock_indicator(current_lock);
        need_flush = true;
    }

    // 3. Modifier pills update
    uint8_t current_mods = get_mods() | get_weak_mods();
#ifndef NO_ACTION_ONESHOT
    current_mods |= get_oneshot_mods();
#endif
    if (current_mods != last_mods) {
        last_mods = current_mods;
        update_modifier_pills(current_mods);
        need_flush = true;
    }

#ifdef WPM_ENABLE
    // 4. WPM update (throttled to at most once per 100ms)
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

void suspend_power_down_kb(void) {
    rgb_matrix_set_suspend_state(true);
#if defined(LCD_BLK_PIN) && LCD_BLK_PIN != -1
    gpio_write_pin_low(LCD_BLK_PIN);
#endif
    qp_power(lcd, false);
    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    if (!display_stealth_mode) {
        qp_power(lcd, true);
#if defined(LCD_BLK_PIN) && LCD_BLK_PIN != -1
        gpio_write_pin_high(LCD_BLK_PIN);
#endif
    }
    rgb_matrix_set_suspend_state(false);
    suspend_wakeup_init_user();
}

#ifdef CHORDAL_HOLD
char chordal_hold_handedness(keypos_t key) {
    if (key.row == 3 || key.row == 7) {
        return '*'; // Thumb clusters can chord with any hand
    }
    return key.row < 4 ? 'L' : 'R';
}
#endif
