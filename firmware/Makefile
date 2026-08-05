.PHONY: build upload monitor clean check

all: upload

build:
	pio run

upload: build
	pio run -t upload

monitor:
	pio device monitor --filter esp32_exception_decoder

clean:
	pio run -t clean

check:
	pio check --fail-on-defect=high
