# Cross-platform settings (Linux/macOS and Windows)
# On Windows, GNU Make drives Git-for-Windows' bash, which provides
# sed/grep/cp used by the version-bump and publish recipes.
ifeq ($(OS),Windows_NT)
	# ezwinports make is a 32-bit binary, so $(PROGRAMFILES) can resolve to
	# "Program Files (x86)" under WoW64. Use 8.3 short names (space-free,
	# redirection-proof) and verify the path really exists via $(wildcard).
	GIT_BASH := $(firstword $(foreach d,C:/PROGRA~1 C:/PROGRA~2,$(wildcard $d/Git/usr/bin/bash.exe)))
	ifeq ($(strip $(GIT_BASH)),)
		GIT_BASH := bash.exe
	endif
	SHELL := $(GIT_BASH)
	# make-spawned bash doesn't get Git's Unix tools on PATH by itself
	export PATH := $(patsubst %/,%,$(dir $(GIT_BASH)));$(PATH)
	PORT ?=
	VENV_SUBDIR := Scripts
	BASE_PYTHON ?= python
else
	SHELL := /bin/bash
	PORT ?= /dev/ttyUSB1
	VENV_SUBDIR := bin
	BASE_PYTHON ?= python3
endif

BAUD ?= 115200
VENV := .venv
PY := $(VENV)/$(VENV_SUBDIR)/python
CONFIG := src/config.h
BACKEND := $(firstword $(wildcard ../backend ../IndietroTuttaBackend))
OTA_DIR := $(BACKEND)/public/ota
FIRMWARE := .pio/build/esp32dev/firmware.bin

ifneq ($(strip $(PORT)),)
	UPLOAD_ARGS := --upload-port $(PORT)
	MONITOR_ARGS := --port $(PORT)
endif

.PHONY: venv install compile build bump-version dist upload monitor clean watch git-push deploy all

venv:
	$(BASE_PYTHON) -m venv $(VENV)
	$(PY) -m pip install -U pip setuptools wheel

install: venv
	$(PY) -m pip install platformio

bump-version:
	@echo "Incrementing firmware version..."
	@VERSION=$$(grep -E '^[[:space:]]*#[[:space:]]*define[[:space:]]+BUILD_VERSION[[:space:]]+"' $(CONFIG) | sed -E 's/.*BUILD_VERSION[[:space:]]+"([^"]+)".*/\1/'); \
	IFS='.' read -r MAJOR MINOR PATCH <<< "$$VERSION"; \
	PATCH=$$((PATCH + 1)); \
	NEW_VERSION="$$MAJOR.$$MINOR.$$PATCH"; \
	if [ -z "$$MAJOR" ]; then \
		echo "ERROR: BUILD_VERSION not found in $(CONFIG)"; \
		exit 1; \
	fi; \
	sed -i -E "s/(^[[:space:]]*#[[:space:]]*define[[:space:]]+BUILD_VERSION[[:space:]]+\")[^\"]*(\".*$$)/\1$$NEW_VERSION\2/" $(CONFIG); \
	echo "Firmware version: $$VERSION -> $$NEW_VERSION"

compile:
	$(PY) -m platformio run

build: compile

dist: bump-version compile git-push

upload:
	$(PY) -m platformio run -t upload $(UPLOAD_ARGS)

monitor:
	$(PY) -m platformio device monitor $(MONITOR_ARGS) --baud $(BAUD)

clean:
	$(PY) -m platformio run -t clean

deploy: compile upload

all: compile upload monitor

git-push:
	@set -e; \
	VERSION=$$(grep -E '^[[:space:]]*#[[:space:]]*define[[:space:]]+BUILD_VERSION[[:space:]]+"' $(CONFIG) | sed -E 's/.*BUILD_VERSION[[:space:]]+"([^"]+)".*/\1/'); \
	if [ -z "$$VERSION" ]; then \
		echo "ERROR: BUILD_VERSION not found"; \
		exit 1; \
	fi; \
	if [ ! -f "$(FIRMWARE)" ]; then \
		echo "ERROR: firmware.bin not found. Run 'make build' first."; \
		exit 1; \
	fi; \
	echo "Publishing firmware version $$VERSION"; \
	echo "Copying firmware.bin..."; \
	cp "$(FIRMWARE)" "$(OTA_DIR)/firmware.bin"; \
	echo "Creating latest.txt..."; \
	echo "$$VERSION" > "$(OTA_DIR)/latest.txt"; \
	echo "Adding OTA files to git..."; \
	git -C "$(BACKEND)" add public/ota/latest.txt public/ota/firmware.bin; \
	echo "Committing..."; \
	git -C "$(BACKEND)" commit -m "Update OTA firmware to version $$VERSION"; \
	echo "Pushing..."; \
	git -C "$(BACKEND)" push; \
	echo "Firmware version $$VERSION published successfully."

watch:
	@command -v entr >/dev/null 2>&1 || (echo "Please install 'entr' or use scripts/watch.sh" && exit 1)
	find src include platformio.ini | entr -r make upload
