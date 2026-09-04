# macropad

A single-button QMK macro/automation device on a genuine RP2040 (Freenove
Pico-compatible board). Pressing the button toggles automation on/off; an LED
reflects the state; while on, a timer periodically fires a keystroke.

* Keyboard Maintainer: [nacitar](https://github.com/nacitar)
* Hardware Supported: Freenove RP2040 board (Raspberry Pi Pico form factor)
* Hardware Availability: handwired, one-off

Make example for this keyboard (after setting up your build environment):

    make nacitar/macropad:default

Flashing (hold BOOTSEL, plug in USB, release, then):

    qmk flash -kb nacitar/macropad -km default

See the [flashing instructions](https://docs.qmk.fm/flashing) for more information.
