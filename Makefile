SHELL := /bin/bash
PORT ?= /dev/ttyUSB0
BAUD ?= 115200
VENV := .venv
PY := $(VENV)/bin/python
PIP := $(VENV)/bin/pip
CONFIG := src/config.h
BACKEND := ../backend
OTA_DIR := $(BACKEND)/public/ota
FIRMWARE := .pio/build/esp32dev/firmware.bin

.PHONY: venv install build bump-version dist upload monitor clean watch

venv:
	python3 -m venv $(VENV)
	$(PIP) install -U pip setuptools wheel

install: venv
	$(PIP) install platformio

bump-version:
	@echo "Incrementing firmware version..."
	@VERSION=$$(grep -E '^[[:space:]]*#define[[:space:]]+BUILD_VERSION[[:space:]]+"' $(CONFIG) | sed -E 's/.*BUILD_VERSION[[:space:]]+"([^"]+)".*/\1/'); \
	IFS='.' read -r MAJOR MINOR PATCH <<< "$$VERSION"; \
	PATCH=$$((PATCH + 1)); \
	NEW_VERSION="$$MAJOR.$$MINOR.$$PATCH"; \
	sed -i -E "s/(^[[:space:]]*#define[[:space:]]+BUILD_VERSION[[:space:]]+\")[^\"]*(\".*$$)/\1$$NEW_VERSION\2/" $(CONFIG); \
	echo "Firmware version: $$VERSION -> $$NEW_VERSION"

build: bump-version
	$(PY) -m platformio run

upload:
	$(PY) -m platformio run -t upload --upload-port $(PORT)

monitor:
	$(PY) -m platformio device monitor --port $(PORT) --baud $(BAUD)

clean:
	$(PY) -m platformio run -t clean

deploy: build upload

all: build upload monitor

dist:
	@VERSION=$$(grep -E '^[[:space:]]*#define[[:space:]]+BUILD_VERSION[[:space:]]+"' $(CONFIG) | sed -E 's/.*BUILD_VERSION[[:space:]]+"([^"]+)".*/\1/'); \
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
