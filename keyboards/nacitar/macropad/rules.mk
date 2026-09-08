# Lets AUTOMATION_MODE be selected from the command line (see the top-level
# Makefile's MODE variable) instead of hand-editing keymap.c, by turning the
# make variable into a compiler -D define. keymap.c's own #define is guarded
# with #ifndef so this takes precedence when set.
ifneq ($(strip $(AUTOMATION_MODE)),)
    OPT_DEFS += -DAUTOMATION_MODE=$(AUTOMATION_MODE)
endif

# Same pattern for the status LED's PWM brightness — see the top-level
# Makefile's BRIGHTNESS variable.
ifneq ($(strip $(STATUS_LED_BRIGHTNESS)),)
    OPT_DEFS += -DSTATUS_LED_BRIGHTNESS=$(STATUS_LED_BRIGHTNESS)
endif

# hardware/pwm.h isn't on the include path by default (RP2040.mk only adds
# the pico-sdk dirs it needs for its own ChibiOS/USB glue). Every function
# we need from it is `static inline` in the header itself, so no extra SRC
# entry is required — just the include dir, same TOP_DIR-relative pattern
# platforms/chibios/vendors/RP/RP2040.mk itself uses for its own pico-sdk
# include dirs.
EXTRAINCDIRS += $(TOP_DIR)/lib/pico-sdk/src/rp2_common/hardware_pwm/include
