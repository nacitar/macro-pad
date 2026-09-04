/* Copyright 2026 nacitar
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include QMK_KEYBOARD_H
#include "gpio.h"

/* ---- Wiring --------------------------------------------------------------
 * Update these once the enclosure is drilled and the LED is wired to its
 * own GPIO. Until then this targets the Pico's onboard LED (GP25) so the
 * toggle/timer logic can be verified with no extra hardware attached.
 * -------------------------------------------------------------------------- */
#define STATUS_LED_PIN GP25 /* -> external LED GPIO (e.g. GP3) once wired */
#define STATUS_LED_ACTIVE_HIGH true

/* ---- Automation tuning ----------------------------------------------------
 * AUTOMATION_START_ENABLED defaults to true because no button is wired yet:
 * the device demonstrates itself on plug-in. Once the button is attached,
 * flip this to false so the real button press is what turns it on.
 * -------------------------------------------------------------------------- */
#define AUTOMATION_INTERVAL_MS 30000
#define AUTOMATION_START_ENABLED true

enum custom_keycodes {
    MP_TOGGLE = SAFE_RANGE,
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(MP_TOGGLE),
};

static bool     automation_enabled = AUTOMATION_START_ENABLED;
static uint32_t last_fire_time     = 0;

/* ============================================================================
 * >>> THE EXTENSION POINT <<<
 * Replace the body of this function with whatever should fire on each tick:
 *   - tap_code(KC_X) / tap_code16(...)   for a single key
 *   - SEND_STRING("...")                 for a literal sequence
 *   - register_code(...)/unregister_code(...) pairs for held modifiers
 * This is the only function that needs to change once the real automation
 * is decided.
 * ============================================================================ */
static void automation_tick(void) {
    tap_code(KC_F15);
}

static void status_led_set(bool on) {
    gpio_write_pin(STATUS_LED_PIN, STATUS_LED_ACTIVE_HIGH ? on : !on);
}

static void automation_set(bool enabled) {
    automation_enabled = enabled;
    last_fire_time      = timer_read32();
    status_led_set(enabled);
}

void keyboard_post_init_user(void) {
    gpio_set_pin_output(STATUS_LED_PIN);
    automation_set(AUTOMATION_START_ENABLED);
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case MP_TOGGLE:
            if (record->event.pressed) {
                automation_set(!automation_enabled);
            }
            return false; /* consume — the button never emits a real keycode */
    }
    return true;
}

void matrix_scan_user(void) {
    if (!automation_enabled) {
        return;
    }
    if (timer_elapsed32(last_fire_time) < AUTOMATION_INTERVAL_MS) {
        return;
    }
    last_fire_time = timer_read32();
    automation_tick();
}
