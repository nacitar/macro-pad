# macro-pad

A single-button QMK macro/automation device on a Freenove RP2040 (Raspberry Pi
Pico-compatible) board. See [`/home/nacitar/.claude/plans`] history or
`keyboards/nacitar/macropad/readme.md` for the hardware/firmware design.

## Setup

Dependencies:

* [`uv`](https://docs.astral.sh/uv/) — manages the `qmk` CLI in a project-local venv.
* `arm-none-eabi-gcc`, `arm-none-eabi-newlib`, `arm-none-eabi-binutils` (pacman) —
  the ARM cross toolchain QMK's build uses.
* `udisks2` (pacman, usually already installed) — used by `scripts/flash.sh` to
  mount the RP2040's BOOTSEL mass-storage volume.

```sh
sudo pacman -S --needed arm-none-eabi-gcc arm-none-eabi-newlib arm-none-eabi-binutils
uv sync
git submodule update --init --recursive
make link      # symlinks keyboards/nacitar into the qmk_firmware submodule
make doctor    # sanity-check the toolchain
```

## Building

```sh
make build     # -> qmk_firmware/.build/nacitar_macropad_default.uf2
make info      # show resolved keyboard.json (pins, VID/PID, etc)
```

## Flashing

Hold **BOOTSEL**, plug the board into USB, release BOOTSEL, then:

```sh
make flash
```

This mounts the `RPI-RP2` volume (via `udisksctl`) if needed and runs `qmk flash`
— no drag-and-drop required.

## Layout

```
keyboards/nacitar/macropad/   <- firmware source (tracked in this repo)
qmk_firmware/                 <- QMK, as a git submodule (pinned revision)
```

`keyboards/nacitar/macropad` is symlinked into `qmk_firmware/keyboards/nacitar`
(created by `make link`) because QMK's build tooling requires keyboard
definitions to live inside its own tree. The symlink is untracked and excluded
locally via the submodule's `.git/info/exclude`, so it never shows up as a dirty
submodule.
