FQBN := esp32:esp32:esp32c3:PartitionScheme=min_spiffs
PORT ?= $(shell ls /dev/cu.usbmodem* 2>/dev/null | head -n 1)
BUILD_DIR := build
FW := firmware.bin

all: verify

verify:
	arduino-cli compile \
		--fqbn "$(FQBN)" \
		--jobs 8 \
		--output-dir $(BUILD_DIR) \
		.

# собрать и скопировать основной бинарник
bin: verify
	@cp $(BUILD_DIR)/*.ino.bin $(FW) 2>/dev/null || cp $(BUILD_DIR)/*.bin $(FW)
	@echo "Firmware ready: $(FW)"

upload:
	arduino-cli upload \
		-p $(PORT) \
		--fqbn "$(FQBN)" \
		--input-dir $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(FW)
