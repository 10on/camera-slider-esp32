.PHONY: build upload flash monitor clean check

# Default: OTA upload
all: upload

build:
	pio run -e usb

upload:
	pio run -e ota -t upload

# USB flash — first flash on blank hardware, or recovery when OTA is unreachable.
flash: build
	pio run -e usb -t upload

monitor:
	pio device monitor --filter esp32_exception_decoder

clean:
	pio run -t clean

check:
	pio check --fail-on-defect=high
