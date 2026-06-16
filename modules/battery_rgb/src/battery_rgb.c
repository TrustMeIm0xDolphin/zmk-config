#include <zephyr/kernel.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/rgb_underglow.h>
#include <zmk/event_manager.h>

static bool low_active;
static bool blink_on;

static void blink_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(blink_work, blink_handler);

static void blink_handler(struct k_work *work) {
    if (!low_active) {
        return;
    }
    if (blink_on) {
        zmk_rgb_underglow_off();
        blink_on = false;
        k_work_schedule(&blink_work, K_MSEC(CONFIG_ZMK_BATTERY_RGB_BLINK_OFF_MS));
    } else {
        zmk_rgb_underglow_on();
        blink_on = true;
        k_work_schedule(&blink_work, K_MSEC(CONFIG_ZMK_BATTERY_RGB_BLINK_ON_MS));
    }
}

static int battery_state_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    if (!ev) {
        return 0;
    }

    bool now_low = ev->state_of_charge < CONFIG_ZMK_BATTERY_RGB_BLINK_THRESHOLD;

    if (now_low && !low_active) {
        low_active = true;
        blink_on = false;
        /* Set color to red once; saved to flash, used by all subsequent on() calls. */
        zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 0, .s = 100, .b = 30});
        k_work_schedule(&blink_work, K_NO_WAIT);
    } else if (!now_low && low_active) {
        low_active = false;
        k_work_cancel_delayable(&blink_work);
        if (blink_on) {
            zmk_rgb_underglow_off();
            blink_on = false;
        }
    }

    return 0;
}

ZMK_LISTENER(battery_rgb_blink, battery_state_listener);
ZMK_SUBSCRIPTION(battery_rgb_blink, zmk_battery_state_changed);
