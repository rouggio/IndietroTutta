PORT ?= /dev/ttyUSB0
BAUD ?= 115200
VENV := .venv
PY := $(VENV)/bin/python
PIP := $(VENV)/bin/pip

.PHONY: venv install build upload monitor clean watch

venv:
	python3 -m venv $(VENV)
	$(PIP) install -U pip setuptools wheel

install: venv
	$(PIP) install platformio

build:
	$(PY) -m platformio run

upload:
	$(PY) -m platformio run -t upload --upload-port $(PORT)

monitor:
	$(PY) -m platformio device monitor --port $(PORT) --baud $(BAUD)

clean:
	$(PY) -m platformio run -t clean

deploy: build upload

all: build upload monitor

watch:
	@command -v entr >/dev/null 2>&1 || (echo "Please install 'entr' or use scripts/watch.sh" && exit 1)
	find src include platformio.ini | entr -r make upload
