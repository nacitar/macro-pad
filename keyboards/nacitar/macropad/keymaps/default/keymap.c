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
#include "hardware/gpio.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"

/* ---- Wiring --------------------------------------------------------------
 * Update these once the enclosure is drilled and the LED is wired to its
 * own GPIO. Until then this targets the Pico's onboard LED (GP25) so the
 * toggle/timer logic can be verified with no extra hardware attached.
 * -------------------------------------------------------------------------- */
#define STATUS_LED_PIN GP25 /* -> external LED GPIO (e.g. GP3) once wired */
#define STATUS_LED_ACTIVE_HIGH true

/* ---- Automation tuning ----------------------------------------------------
 * A real toggle source (BOOTSEL, below) exists, so start disabled and let
 * a press turn it on — matches the eventual button-driven behavior.
 * -------------------------------------------------------------------------- */
#define AUTOMATION_INTERVAL_MS 30000
#define AUTOMATION_START_ENABLED false
#define BOOTSEL_POLL_INTERVAL_MS 20

/* ---- Automation payload ----------------------------------------------------
 * Pick what automation_tick() sends with `make build MODE=<fkey|intl|mouse>`
 * (see the top-level Makefile) — no file editing required. AUTOMATION_MODE
 * is passed in as a compiler -D define when MODE is given; the #ifndef below
 * only supplies a default for a plain `make build` with no MODE argument.
 * AUTOMATION_MODE_MOUSE_JIGGLE additionally requires "mousekey": true in
 * keyboard.json (enforced below); MODE=mouse sets that automatically too.
 * -------------------------------------------------------------------------- */
/* Values start at 1, not 0: the preprocessor treats an undefined identifier
 * used in #if as 0, so a misspelled AUTOMATION_MODE would otherwise silently
 * alias whichever mode was assigned 0 instead of hitting the #error below. */
#define AUTOMATION_MODE_FKEY 1         /* tap an unused F-key (current default) */
#define AUTOMATION_MODE_INTL_KEY 2     /* tap a JIS/Korean-only key; inert on US layouts */
#define AUTOMATION_MODE_MOUSE_JIGGLE 3 /* +1/-1 mouse move; nets zero, needs "mousekey": true */

#ifndef AUTOMATION_MODE
#    define AUTOMATION_MODE AUTOMATION_MODE_FKEY
#endif

#if AUTOMATION_MODE == AUTOMATION_MODE_MOUSE_JIGGLE && !defined(MOUSE_ENABLE)
#    error "AUTOMATION_MODE_MOUSE_JIGGLE requires \"mousekey\": true in keyboards/nacitar/macropad/keyboard.json"
#endif

enum custom_keycodes {
    MP_TOGGLE = SAFE_RANGE,
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(MP_TOGGLE),
};

static bool     automation_enabled = AUTOMATION_START_ENABLED;
static uint32_t last_fire_time     = 0;
static uint32_t last_bootsel_poll  = 0;
static bool     bootsel_was_pressed = false;

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
#if AUTOMATION_MODE == AUTOMATION_MODE_FKEY
    tap_code(KC_F15);
#elif AUTOMATION_MODE == AUTOMATION_MODE_INTL_KEY
    tap_code(KC_INTERNATIONAL_1);
#elif AUTOMATION_MODE == AUTOMATION_MODE_MOUSE_JIGGLE
    report_mouse_t report = {0};
    report.x = 1;
    host_mouse_send(&report);
    report.x = -1;
    host_mouse_send(&report);
    report.x = 0;
    host_mouse_send(&report);
#else
#    error "Unrecognized AUTOMATION_MODE value — check it's spelled exactly as one of the AUTOMATION_MODE_* constants above"
#endif
}

/* BOOTSEL shares the flash chip-select line, not a normal GPIO — reading it
 * means briefly floating that line from RAM-resident code while flash (and
 * therefore this very function, if it weren't RAM-resident) is unreachable.
 * Standard Pico SDK technique; QMK's own RP2040 flash driver uses the same
 * approach. This only samples the pin — it can never re-enter the ROM
 * bootloader, which still requires a real power-on/reset to trigger. */
static bool __no_inline_not_in_flash_func(bootsel_pressed)(void) {
    const uint32_t cs_pin_index = 1;
    uint32_t       flags        = save_and_disable_interrupts();
    hw_write_masked(&ioqspi_hw->io[cs_pin_index].ctrl, GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB, IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 1000; ++i) {
    }
    bool pressed = !(sio_hw->gpio_hi_in & (1u << 1));
    hw_write_masked(&ioqspi_hw->io[cs_pin_index].ctrl, GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB, IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    restore_interrupts(flags);
    return pressed;
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
    /* Temporary bring-up toggle: BOOTSEL, until a real button is wired.
     * Independent of the key matrix — bypasses process_record_user
     * entirely and toggles automation directly on the press edge. */
    if (timer_elapsed32(last_bootsel_poll) >= BOOTSEL_POLL_INTERVAL_MS) {
        last_bootsel_poll   = timer_read32();
        bool pressed        = bootsel_pressed();
        if (pressed && !bootsel_was_pressed) {
            automation_set(!automation_enabled);
        }
        bootsel_was_pressed = pressed;
    }

    if (!automation_enabled) {
        return;
    }
    if (timer_elapsed32(last_fire_time) < AUTOMATION_INTERVAL_MS) {
        return;
    }
    last_fire_time = timer_read32();
    automation_tick();
}
