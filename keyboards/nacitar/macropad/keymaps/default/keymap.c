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
#include <hal.h> /* hal_lld_peripheral_unreset() — see status_led_init() */
#include "hardware/gpio.h"
/* QMK_KEYBOARD_H pulls in ChibiOS's own RP2040 CMSIS header, which #defines
 * PWM as a peripheral-struct pointer (lib/chibios/os/common/ext/RP/RP2040/
 * rp2040.h) — that's a different, incompatible use of the bare identifier
 * "PWM" than pico-sdk's hardware/pwm.h relies on internally (an assertion-
 * group name pasted via ##). We never use ChibiOS's macro, so drop it
 * before pico-sdk's header needs the name back. */
#undef PWM
#include "hardware/pwm.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include <stdlib.h>

/* ---- Wiring --------------------------------------------------------------
 * Update these once the enclosure is drilled and the LED is wired to its
 * own GPIO. Until then this targets the Pico's onboard LED (GP25) so the
 * toggle/timer logic can be verified with no extra hardware attached.
 * GP25 is an ordinary GPIO (not a fixed-function pin), so it — and
 * whatever pin replaces it — can always be driven by the RP2040's PWM
 * hardware for brightness control, not just on/off.
 * -------------------------------------------------------------------------- */
#define STATUS_LED_PIN GP25 /* -> external LED GPIO (e.g. GP3) once wired */
#define STATUS_LED_ACTIVE_HIGH true
#define STATUS_LED_PWM_WRAP 255 /* 8-bit brightness resolution */

/* Brightness, 0-100, set with `make build BRIGHTNESS=<n>` (see the
 * top-level Makefile) — no file editing required, same -D-define pattern
 * as AUTOMATION_MODE above. */
#ifndef STATUS_LED_BRIGHTNESS
#    define STATUS_LED_BRIGHTNESS 50
#endif

#if STATUS_LED_BRIGHTNESS < 0 || STATUS_LED_BRIGHTNESS > 100
#    error "STATUS_LED_BRIGHTNESS must be between 0 and 100"
#endif

/* ---- Automation tuning ----------------------------------------------------
 * A real toggle source (BOOTSEL, below) exists, so start disabled and let
 * a press turn it on — matches the eventual button-driven behavior.
 *
 * Interval is randomized (uniformly) between MIN and MAX on every firing,
 * not fixed, so the automation doesn't look like a metronome.
 * -------------------------------------------------------------------------- */
#define AUTOMATION_INTERVAL_MIN_MS 60000  /* 1 minute */
#define AUTOMATION_INTERVAL_MAX_MS 240000 /* 4 minutes */
#define AUTOMATION_START_ENABLED false
#define BOOTSEL_POLL_INTERVAL_MS 20

/* ---- Automation payload ----------------------------------------------------
 * Pick what automation_tick() sends with `make build MODE=<scroll|mouse>`
 * (see the top-level Makefile) — no file editing required. AUTOMATION_MODE
 * is passed in as a compiler -D define when MODE is given; the #ifndef below
 * only supplies a default for a plain `make build` with no MODE argument.
 * AUTOMATION_MODE_MOUSE_JIGGLE additionally requires "mousekey": true in
 * keyboard.json (enforced below); MODE=mouse sets that automatically too.
 * -------------------------------------------------------------------------- */
/* Values start at 1, not 0: the preprocessor treats an undefined identifier
 * used in #if as 0, so a misspelled AUTOMATION_MODE would otherwise silently
 * alias whichever mode was assigned 0 instead of hitting the #error below. */
#define AUTOMATION_MODE_SCROLLING 1    /* random page/arrow scrolling, bounded and reversible (default) */
#define AUTOMATION_MODE_MOUSE_JIGGLE 2 /* random bounded mouse drift; needs "mousekey": true */

#ifndef AUTOMATION_MODE
#    define AUTOMATION_MODE AUTOMATION_MODE_SCROLLING
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
static uint32_t next_interval_ms   = AUTOMATION_INTERVAL_MIN_MS;
static uint32_t last_bootsel_poll  = 0;
static bool     bootsel_was_pressed = false;

/* ============================================================================
 * Bounded reversible list — shared random-walk engine behind both
 * AUTOMATION_MODE_SCROLLING and AUTOMATION_MODE_MOUSE_JIGGLE below. An
 * "entry" is up to two independent int8_t values (a direction bit for
 * scrolling; a (dx, dy) pair for mouse jiggle — unused fields just stay 0).
 *
 * On each tick, given a list of at most BOUNDED_LIST_CAPACITY entries:
 *   - empty list -> always generate() + apply() a new entry
 *   - full list   -> always pick an existing entry at random, apply() it
 *                    inverted (undoing it), and remove it
 *   - otherwise    -> coin flip between the two
 * Since removal only happens by undoing exactly what was added, and only
 * pushes grow the list, net displacement from the starting point can never
 * exceed BOUNDED_LIST_CAPACITY entries — it wanders, but stays on a leash.
 * Callers supply generate() (build a new entry) and apply() (send it, or
 * send its inverse) — the list bookkeeping and the push/pop decision are
 * identical either way, this is the only copy of that logic.
 * ============================================================================ */
#define BOUNDED_LIST_CAPACITY 10

typedef struct {
    int8_t a;
    int8_t b;
} bounded_list_entry_t;

typedef struct {
    bounded_list_entry_t entries[BOUNDED_LIST_CAPACITY];
    uint8_t               count;
} bounded_list_t;

typedef bounded_list_entry_t (*bounded_list_generate_fn)(void *ctx);
typedef void (*bounded_list_apply_fn)(void *ctx, bounded_list_entry_t entry, bool invert);

static void bounded_list_clear(bounded_list_t *list) {
    list->count = 0;
}

static void bounded_list_tick(bounded_list_t *list, void *ctx, bounded_list_generate_fn generate, bounded_list_apply_fn apply) {
    bool push;
    if (list->count == 0) {
        push = true;
    } else if (list->count >= BOUNDED_LIST_CAPACITY) {
        push = false;
    } else {
        push = rand() % 2;
    }

    if (push) {
        bounded_list_entry_t entry = generate(ctx);
        apply(ctx, entry, false);
        list->entries[list->count++] = entry;
    } else {
        uint8_t               idx   = rand() % list->count;
        bounded_list_entry_t  entry = list->entries[idx];
        list->entries[idx]          = list->entries[--list->count]; /* swap-remove */
        apply(ctx, entry, true);                                    /* inverted: undo it */
    }
}

#if AUTOMATION_MODE == AUTOMATION_MODE_SCROLLING
/* ---- Scrolling mode -------------------------------------------------------
 * Two independent bounded lists: page up/down, and arrow up/down. Each
 * entry's `a` field is the direction (0 = up, 1 = down); `b` is unused.
 * automation_tick() picks one of the two lists at random each firing.
 * -------------------------------------------------------------------------- */
typedef struct {
    uint8_t key_up;
    uint8_t key_down;
} scroll_ctx_t;

static bounded_list_t     page_list   = {0};
static bounded_list_t     arrow_list  = {0};
static const scroll_ctx_t page_ctx    = {KC_PGUP, KC_PGDN};
static const scroll_ctx_t arrow_ctx   = {KC_UP, KC_DOWN};

static bounded_list_entry_t scroll_generate(void *ctx) {
    (void)ctx;
    return (bounded_list_entry_t){.a = (int8_t)(rand() % 2)};
}

static void scroll_apply(void *ctx, bounded_list_entry_t entry, bool invert) {
    const scroll_ctx_t *sctx = (const scroll_ctx_t *)ctx;
    bool                 down = entry.a;
    if (invert) {
        down = !down;
    }
    tap_code(down ? sctx->key_down : sctx->key_up);
}
#endif

#if AUTOMATION_MODE == AUTOMATION_MODE_MOUSE_JIGGLE
/* ---- Mouse jiggle mode -----------------------------------------------------
 * One bounded list of past relative moves. Each entry's `a`/`b` are the
 * (dx, dy) delta, independently in [-MOUSE_JIGGLE_MAX_UNITS,
 * MOUSE_JIGGLE_MAX_UNITS] but never both 0. Net displacement on either axis
 * can never exceed BOUNDED_LIST_CAPACITY * MOUSE_JIGGLE_MAX_UNITS units.
 * -------------------------------------------------------------------------- */
#    define MOUSE_JIGGLE_MAX_UNITS 5

static bounded_list_t mouse_jiggle_list = {0};

/* Uniform in [-MOUSE_JIGGLE_MAX_UNITS, MOUSE_JIGGLE_MAX_UNITS]. */
static int8_t mouse_jiggle_random_axis(void) {
    return (int8_t)(rand() % (2 * MOUSE_JIGGLE_MAX_UNITS + 1)) - MOUSE_JIGGLE_MAX_UNITS;
}

static bounded_list_entry_t mouse_jiggle_generate(void *ctx) {
    (void)ctx;
    bounded_list_entry_t entry;
    do {
        entry.a = mouse_jiggle_random_axis();
        entry.b = mouse_jiggle_random_axis();
    } while (entry.a == 0 && entry.b == 0); /* (0, 0) isn't a valid move */
    return entry;
}

static void mouse_jiggle_apply(void *ctx, bounded_list_entry_t entry, bool invert) {
    (void)ctx;
    report_mouse_t report = {0};
    report.x              = invert ? -entry.a : entry.a;
    report.y              = invert ? -entry.b : entry.b;
    host_mouse_send(&report);
}
#endif

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
#if AUTOMATION_MODE == AUTOMATION_MODE_SCROLLING
    if (rand() % 2) {
        bounded_list_tick(&page_list, (void *)&page_ctx, scroll_generate, scroll_apply);
    } else {
        bounded_list_tick(&arrow_list, (void *)&arrow_ctx, scroll_generate, scroll_apply);
    }
#elif AUTOMATION_MODE == AUTOMATION_MODE_MOUSE_JIGGLE
    bounded_list_tick(&mouse_jiggle_list, NULL, mouse_jiggle_generate, mouse_jiggle_apply);
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

static void status_led_init(void) {
    /* QMK's ChibiOS build never enables ChibiOS's own PWM driver
     * (RP_PWM_USE_PWM* are all FALSE in mcuconf.h — this board never
     * needed PWM before), and each ChibiOS RP2040 peripheral driver is
     * responsible for taking its own hardware block out of reset (see
     * hal_pal_lld.c doing the same for IO_BANK0/PADS_BANK0). With no PWM
     * driver enabled, nothing ever does that for the PWM block, so it's
     * still held in hardware reset here — pico-sdk's pwm_* calls below
     * would otherwise be writing to registers that can't respond. */
    hal_lld_peripheral_unreset(RESETS_ALLREG_PWM);

    gpio_set_function(STATUS_LED_PIN, GPIO_FUNC_PWM);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, STATUS_LED_PWM_WRAP);
    pwm_init(pwm_gpio_to_slice_num(STATUS_LED_PIN), &config, true);
}

static void status_led_set(bool on) {
    uint16_t level;
    if (on) {
        uint16_t on_level = (STATUS_LED_PWM_WRAP * STATUS_LED_BRIGHTNESS) / 100;
        level             = STATUS_LED_ACTIVE_HIGH ? on_level : (STATUS_LED_PWM_WRAP - on_level);
    } else {
        level = STATUS_LED_ACTIVE_HIGH ? 0 : STATUS_LED_PWM_WRAP;
    }
    pwm_set_gpio_level(STATUS_LED_PIN, level);
}

/* Uniform in [AUTOMATION_INTERVAL_MIN_MS, AUTOMATION_INTERVAL_MAX_MS]. */
static uint32_t random_interval_ms(void) {
    return AUTOMATION_INTERVAL_MIN_MS + (rand() % (AUTOMATION_INTERVAL_MAX_MS - AUTOMATION_INTERVAL_MIN_MS + 1));
}

static void automation_set(bool enabled) {
    automation_enabled = enabled;
    last_fire_time      = timer_read32();
    if (enabled) {
        /* Reseed on every enable, not just once at boot: timer_read32() at
         * power-on is always near-zero (same seed every time), but the
         * moment a user actually presses the toggle is unpredictable. */
        srand(timer_read32());
        next_interval_ms = random_interval_ms();
    } else {
        /* Start fresh next time: don't carry a half-undone walk across a
         * disable/enable cycle. */
#if AUTOMATION_MODE == AUTOMATION_MODE_SCROLLING
        bounded_list_clear(&page_list);
        bounded_list_clear(&arrow_list);
#elif AUTOMATION_MODE == AUTOMATION_MODE_MOUSE_JIGGLE
        bounded_list_clear(&mouse_jiggle_list);
#endif
    }
    status_led_set(enabled);
}

void keyboard_post_init_user(void) {
    status_led_init();
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
    if (timer_elapsed32(last_fire_time) < next_interval_ms) {
        return;
    }
    last_fire_time   = timer_read32();
    next_interval_ms = random_interval_ms();
    automation_tick();
}
