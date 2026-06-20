/*
 * Periodic battery level indicator via direct LED strip control.
 *
 * Uses led_strip_update_rgb() directly instead of zmk_rgb_underglow_*().
 * This avoids ZMK's 50ms animation tick (which spams SPI and disrupts BLE)
 * and avoids flash writes entirely.
 *
 * >60%  → green   (r=0,   g=128, b=0)
 * 20–60% → orange  (r=128, g=53,  b=0)
 * ≤20%  → red     (r=128, g=0,   b=0)
 *
 * 3 blinks once per CONFIG_ZMK_BATTERY_RGB_PERIOD_MS.
 * Each split half operates independently on its own battery.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/led_strip.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/event_manager.h>

#define STRIP_NODE       DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_NODE, chain_length)

static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[STRIP_NUM_PIXELS];

#define BLINK_ON_MS    400
#define BLINK_OFF_MS   300
#define NUM_BLINKS     3
#define FIRST_DELAY_MS 15000

/* RGB values at ~50% brightness, pre-computed from HSB. */
static const struct led_rgb COLOR_OFF    = {.r = 0,   .g = 0,  .b = 0};
static const struct led_rgb COLOR_RED    = {.r = 128, .g = 0,  .b = 0};
static const struct led_rgb COLOR_ORANGE = {.r = 128, .g = 53, .b = 0};
static const struct led_rgb COLOR_GREEN  = {.r = 0,   .g = 128,.b = 0};

static uint8_t current_soc = 100;
static int     seq_step;

static void fill_strip(const struct led_rgb *color) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = *color;
    }
    led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
}

static const struct led_rgb *soc_color(uint8_t soc) {
    if (soc > 60) return &COLOR_GREEN;
    if (soc > 20) return &COLOR_ORANGE;
    return &COLOR_RED;
}

/* ── blink sequence ──────────────────────────────────────────────────────── */

static void blink_seq_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(blink_seq_work, blink_seq_handler);

static void blink_seq_handler(struct k_work *work) {
    bool on_phase  = (seq_step % 2 == 0);
    int  blink_num = seq_step / 2;

    if (on_phase) {
        fill_strip(soc_color(current_soc));
        seq_step++;
        k_work_schedule(&blink_seq_work, K_MSEC(BLINK_ON_MS));
    } else {
        fill_strip(&COLOR_OFF);
        seq_step++;
        if (blink_num >= NUM_BLINKS - 1) {
            seq_step = 0; /* done */
        } else {
            k_work_schedule(&blink_seq_work, K_MSEC(BLINK_OFF_MS));
        }
    }
}

/* ── periodic trigger ────────────────────────────────────────────────────── */

static void periodic_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(periodic_work, periodic_handler);

static void periodic_handler(struct k_work *work) {
    seq_step = 0;
    k_work_reschedule(&blink_seq_work, K_NO_WAIT);
    k_work_schedule(&periodic_work, K_MSEC(CONFIG_ZMK_BATTERY_RGB_PERIOD_MS));
}

/* ── battery event listener ──────────────────────────────────────────────── */

static int battery_state_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    if (!ev) return 0;
    current_soc = ev->state_of_charge;
    return 0;
}

ZMK_LISTENER(battery_rgb, battery_state_listener);
ZMK_SUBSCRIPTION(battery_rgb, zmk_battery_state_changed);

/* ── init ────────────────────────────────────────────────────────────────── */

static int battery_rgb_init(void) {
    if (!device_is_ready(strip)) {
        return -ENODEV;
    }
    k_work_schedule(&periodic_work, K_MSEC(FIRST_DELAY_MS));
    return 0;
}

SYS_INIT(battery_rgb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
