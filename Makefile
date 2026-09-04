export QMK_HOME := $(CURDIR)/qmk_firmware
KB := nacitar/macropad
KM := default
SYMLINK := $(QMK_HOME)/keyboards/nacitar

.PHONY: build flash clean doctor info shell link

link:
	@mkdir -p $(QMK_HOME)/keyboards
	@if [ ! -e "$(SYMLINK)" ]; then \
		ln -s ../../keyboards/nacitar "$(SYMLINK)"; \
		gitdir="$$(git -C $(QMK_HOME) rev-parse --absolute-git-dir)"; \
		echo "keyboards/nacitar" >> "$$gitdir/info/exclude"; \
	fi

build: link
	uv run qmk compile -kb $(KB) -km $(KM)

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
