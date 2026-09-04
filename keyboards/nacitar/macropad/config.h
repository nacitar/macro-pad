#pragma once

/* Keep the wire-visible product string honest about what's actually
 * enumerated: "USB Keyboard" for a keyboard-only build, "USB Composite
 * Device" once "mousekey" adds the mouse interface. Both are generic
 * descriptive strings genuinely used across countless unrelated real
 * devices, not a fabricated brand or a specific product.
 *
 * Overrides the keyboard.json-derived default via the #ifndef guard in
 * the generated info_config.h — this file is included before it, since
 * it comes from the keyboard directory rather than the keymap. */
#ifdef MOUSE_ENABLE
#    define PRODUCT "USB Composite Device"
#else
#    define PRODUCT "USB Keyboard"
#endif
