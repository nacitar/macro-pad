# Lets AUTOMATION_MODE be selected from the command line (see the top-level
# Makefile's MODE variable) instead of hand-editing keymap.c, by turning the
# make variable into a compiler -D define. keymap.c's own #define is guarded
# with #ifndef so this takes precedence when set.
ifneq ($(strip $(AUTOMATION_MODE)),)
    OPT_DEFS += -DAUTOMATION_MODE=$(AUTOMATION_MODE)
endif
