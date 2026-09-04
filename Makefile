export QMK_HOME := $(CURDIR)/qmk_firmware
KB := nacitar/macropad
KM := default
SYMLINK := $(QMK_HOME)/keyboards/nacitar

# Which automation payload to build — see AUTOMATION_MODE_* in keymap.c.
# `make build MODE=mouse` etc.; no file editing required.
MODE ?= fkey
ifeq ($(MODE),fkey)
    AUTOMATION_MODE_VALUE := 1
    MOUSEKEY_ENABLE_VALUE := no
else ifeq ($(MODE),intl)
    AUTOMATION_MODE_VALUE := 2
    MOUSEKEY_ENABLE_VALUE := no
else ifeq ($(MODE),mouse)
    AUTOMATION_MODE_VALUE := 3
    MOUSEKEY_ENABLE_VALUE := yes
else
    $(error Unknown MODE "$(MODE)" — expected one of: fkey intl mouse)
endif
export AUTOMATION_MODE_VALUE
export MOUSEKEY_ENABLE_VALUE

.PHONY: build flash clean doctor info shell link

link:
	@mkdir -p $(QMK_HOME)/keyboards
	@if [ ! -e "$(SYMLINK)" ]; then \
		ln -s ../../keyboards/nacitar "$(SYMLINK)"; \
		gitdir="$$(git -C $(QMK_HOME) rev-parse --absolute-git-dir)"; \
		echo "keyboards/nacitar" >> "$$gitdir/info/exclude"; \
	fi

build: link
	uv run qmk compile -kb $(KB) -km $(KM) -e AUTOMATION_MODE=$(AUTOMATION_MODE_VALUE) -e MOUSEKEY_ENABLE=$(MOUSEKEY_ENABLE_VALUE)

flash: link
	./scripts/flash.sh

info: link
	uv run qmk info -kb $(KB) -km $(KM)

doctor:
	uv run qmk doctor

shell:
	uv run --env-file .env bash

clean:
	uv run qmk clean
