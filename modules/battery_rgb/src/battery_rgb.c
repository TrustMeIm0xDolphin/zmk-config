/*
 * Periodic battery level indicator via RGB underglow.
 * Each split half monitors its own battery independently.
 *
 * >60%  → green  (h=120)
 * 20–60% → orange (h=25)
 * ≤20%  → red    (h=0)
 *
 * Shows 3 blinks once per CONFIG_ZMK_BATTERY_RGB_PERIOD_MS.
 * zmk_rgb_underglow_set_hsb() is called only when the color category
 * changes (>60 / 20–60 / ≤20), avoiding unnecessary flash writes.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/rgb_underglow.h>
#include <zmk/event_manager.h>

#define BLINK_ON_MS     400
#define BLINK_OFF_MS    300
#define NUM_BLINKS      3
#define FIRST_DELAY_MS  15000   /* show first indication ~15s after boot */

static uint8_t current_soc    = 100;
static uint8_t prev_category  = 0xFF; /* invalid → forces set_hsb on first run */
static int     seq_step;

static uint8_t soc_category(uint8_t soc) {
    if (soc > 60) return 2;
    if (soc > 20) return 1;
    return 0;
}

static struct zmk_led_hsb category_color(uint8_t cat) {
    switch (cat) {
        case 2:  return (struct zmk_led_hsb){.h = 120, .s = 100, .b = 30}; /* green  */
        case 1:  return (struct zmk_led_hsb){.h = 25,  .s = 100, .b = 30}; /* orange */
        default: return (struct zmk_led_hsb){.h = 0,   .s = 100, .b = 30}; /* red    */
    }
}

/* ── blink sequence ──────────────────────────────────────────────────────── */

static void blink_seq_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(blink_seq_work, blink_seq_handler);

static void blink_seq_handler(struct k_work *work) {
    bool on_phase  = (seq_step % 2 == 0);
    int  blink_num = seq_step / 2;

    if (on_phase) {
        zmk_rgb_underglow_on();
        seq_step++;
        k_work_schedule(&blink_seq_work, K_MSEC(BLINK_ON_MS));
    } else {
        zmk_rgb_underglow_off();
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
    uint8_t cat = soc_category(current_soc);

    /* Only write to flash when the color category actually changes. */
    if (cat != prev_category) {
        zmk_rgb_underglow_set_hsb(category_color(cat));
        prev_category = cat;
    }

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
    k_work_schedule(&periodic_work, K_MSEC(FIRST_DELAY_MS));
    return 0;
}

SYS_INIT(battery_rgb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
