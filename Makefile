# ================================================================
# Paralax — Makefile
#
# Desktop:
#   make            — build desktop example
#   make test       — build and run tests
#   make clean      — remove build artifacts
#
# Arduino (requires arduino-cli):
#   make arduino-cli-setup          — install all board cores
#   make nano   [SERIAL=/dev/tty*]  — compile + upload Arduino Nano
#   make pico   [SERIAL=/dev/tty*]  — compile + upload Pi Pico
#   make esp32  [SERIAL=/dev/tty*]  — compile + upload ESP32
#   make esp8266 [SERIAL=/dev/tty*] — compile + upload ESP8266
#
# Set SERIAL to skip auto-detect:
#   make nano SERIAL=/dev/ttyUSB0
# ================================================================

# --- Desktop ---
TARGET   := paralax
CXX      := g++
CXXFLAGS := -std=c++11 -Wall -Wextra -O3 -Iinclude
BUILDDIR := build

# --- Arduino CLI ---
CLI      := arduino-cli
CLIOPTS  :=

# FQBNs
FQBN_NANO   := arduino:avr:nano
FQBN_PICO   := rp2040:rp2040:rpipico
FQBN_ESP32  := esp32:esp32:esp32
FQBN_ESP8266 := esp8266:esp8266:generic

# Sketch directories
SKETCH_NANO    := examples/arduino/nano/nano_blink
SKETCH_PICO    := examples/arduino/pico/pico_blink
SKETCH_ESP32   := examples/arduino/esp32/esp32_multitask
SKETCH_ESP8266 := examples/arduino/esp8266/esp8266_blink

# Board manager URLs
URL_PICO    := https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
URL_ESP32   := https://espressif.github.io/arduino-esp32/package_esp32_index.json
URL_ESP8266 := https://arduino.esp8266.com/stable/package_esp8266com_index.json

# ================================================================
# Desktop targets
# ================================================================

.PHONY: all clean test
.PHONY: arduino-cli-setup nano pico esp32 esp8266

all: $(BUILDDIR)/$(TARGET)

$(BUILDDIR)/$(TARGET): $(BUILDDIR)/paralax.o $(BUILDDIR)/main.o
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILDDIR)/paralax.o: src/paralax.cpp include/paralax.hpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/main.o: examples/desktop/main.cpp include/paralax.hpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: $(BUILDDIR)/paralax.o tests/paralax_test.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $(BUILDDIR)/test_runner tests/paralax_test.cpp $(BUILDDIR)/paralax.o
	$(BUILDDIR)/test_runner

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
	@# clean copied library files from sketch dirs
	rm -f examples/arduino/*/*/paralax.hpp
	rm -f examples/arduino/*/*/paralax.cpp

# ================================================================
# Arduino CLI setup — install all board cores
# ================================================================

arduino-cli-setup:
	@echo "=== Updating arduino-cli index ==="
	$(CLI) config init --overwrite 2>/dev/null || true
	$(CLI) config add board_manager.additional_urls \
		"$(URL_PICO)" \
		"$(URL_ESP32)" \
		"$(URL_ESP8266)" 2>/dev/null || true
	$(CLI) core update-index
	@echo ""
	@echo "=== Installing board cores ==="
	$(CLI) core install arduino:avr
	$(CLI) core install rp2040:rp2040
	$(CLI) core install esp32:esp32
	$(CLI) core install esp8266:esp8266
	@echo ""
	@echo "=== Installed cores ==="
	$(CLI) core list
	@echo ""
	@echo "Done. You can now run: make nano / make pico / make esp32 / make esp8266"

# ================================================================
# Arduino build helpers
#
# Each target:
#   1. Copies paralax.hpp + paralax.cpp into the sketch folder
#      (arduino-cli compiles all .cpp in the sketch dir)
#   2. Compiles with arduino-cli
#   3. Uploads if SERIAL is set or auto-detected
# ================================================================

define ARDUINO_BUILD
	@echo "=== Copying library files to $(2) ==="
	cp include/paralax.hpp $(2)/paralax.hpp
	cp src/paralax.cpp $(2)/paralax.cpp
	@echo "=== Compiling $(2) for $(1) ==="
	$(CLI) compile --fqbn $(1) $(2) $(CLIOPTS)
	@if [ -n "$(SERIAL)" ]; then \
		echo "=== Uploading to $(SERIAL) ===" ; \
		$(CLI) upload --fqbn $(1) --port $(SERIAL) $(2) ; \
		echo "=== Upload complete ===" ; \
		echo "Monitor with: $(CLI) monitor --port $(SERIAL) --config baudrate=115200" ; \
	else \
		echo "" ; \
		echo "Compile OK. To upload, run:" ; \
		echo "  make $(3) SERIAL=/dev/ttyXXX" ; \
		echo "" ; \
		echo "Or upload manually:" ; \
		echo "  $(CLI) upload --fqbn $(1) --port /dev/ttyXXX $(2)" ; \
	fi
endef

# --- Nano ---
nano:
	$(call ARDUINO_BUILD,$(FQBN_NANO),$(SKETCH_NANO),nano)

# --- Pi Pico ---
pico:
	$(call ARDUINO_BUILD,$(FQBN_PICO),$(SKETCH_PICO),pico)

# --- ESP32 ---
esp32:
	$(call ARDUINO_BUILD,$(FQBN_ESP32),$(SKETCH_ESP32),esp32)

# --- ESP8266 ---
esp8266:
	$(call ARDUINO_BUILD,$(FQBN_ESP8266),$(SKETCH_ESP8266),esp8266)
