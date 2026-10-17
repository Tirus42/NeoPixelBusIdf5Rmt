#pragma once

/**
* NeoPixelBusIdf5Rmt.h
*
* A drop-in NeoPixelBus output method for LedStrip_NeoPixelBus<Feature, Method>
* that drives WS2812x / SK6812 LEDs using the ESP-IDF 5 "new" RMT TX driver
* (driver/rmt_tx.h, a.k.a. "driver_ng").
*
* Why this exists
* ---------------
* NeoPixelBus' built-in NeoEsp32RmtMethod uses the *legacy* RMT driver
* (rmt_driver_install()). ESP-IDF >= 5 refuses to run the legacy driver once
* the new driver has been initialised by another component. On Arduino-ESP32 3.x
* the core ships an RMT HAL based on the new driver, so the built-in RMT method
* aborts at boot with:
*
*     rmt(legacy): CONFLICT! driver_ng is not allowed to be used with the legacy driver
*
* This method uses only the new driver, so it coexists with the core.
*
* NOTE: all ESP-IDF "new RMT" includes live in RmtIdf5ChannelPool.cpp, not here.
* Including driver/rmt_tx.h in the same TU as <NeoPixelBus.h> does not compile
* because NeoPixelBus pulls in the deprecated legacy driver/rmt.h (both define
* rmt_channel_t). See RmtIdf5Driver.h / RmtIdf5ChannelPool.h.
*
* Channel sharing
* ---------------
* All strips of this method share one pool of RMT TX channels
* (RmtIdf5ChannelPool). While there are no more strips than the chip has TX
* channels (2 on ESP32-C3, 4 on S3/C6) every strip gets its own channel; beyond
* that, extra strips share a channel and it is re-pointed to another GPIO on
* demand. This is what lets a board with, say, 3 strips run on a C3.
*
* Buffer / timing notes
* ---------------------
*  - The ESP32-C3 has no RMT DMA (SOC_RMT_SUPPORT_DMA is undefined) and only
*    SOC_RMT_MEM_WORDS_PER_CHANNEL (48) symbols of RMT RAM per channel. A whole
*    200-LED frame (200 * 4 bytes * 8 bits = 6400 symbols) cannot be held in
*    hardware, so the driver streams it and refills via its encoder ISR.
*  - Unlike the legacy driver (which forces a LOWMED interrupt) the new driver
*    lets us choose the interrupt priority via `intr_priority`, so the refill
*    cannot be starved by BLE. Tune InterruptPriority below if needed.
*  - The pixel buffer is sized for the requested LED count (e.g. 200 * 4 = 800
*    bytes) and double buffered so the caller can keep editing during transfer.
*/

#include <Arduino.h>
#include <NeoPixelBus.h>

#include "RmtIdf5Driver.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)

#include "esp_heap_caps.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Speed profiles
//
// 20 MHz resolution -> 50 ns per RMT tick:
//   bit 0: 400 ns high / 850 ns low   -> 8 / 17 ticks
//   bit 1: 800 ns high / 450 ns low   -> 16 / 9 ticks
//   bit period = 1250 ns = 25 ticks (800 kHz)
// ---------------------------------------------------------------------------

struct NeoEsp32Idf5RmtSk6812Speed {
	static constexpr uint32_t ResolutionHz  = 20000000u; // 50 ns / tick
	static constexpr uint16_t Bit0HighTicks = 8;         // 400 ns
	static constexpr uint16_t Bit0LowTicks  = 17;        // 850 ns
	static constexpr uint16_t Bit1HighTicks = 16;        // 800 ns
	static constexpr uint16_t Bit1LowTicks  = 9;         // 450 ns
	static constexpr uint32_t ResetMicros   = 80;        // SK6812 latch
};

struct NeoEsp32Idf5RmtWs2812xSpeed {
	static constexpr uint32_t ResolutionHz  = 20000000u; // 50 ns / tick
	static constexpr uint16_t Bit0HighTicks = 8;         // 400 ns
	static constexpr uint16_t Bit0LowTicks  = 17;        // 850 ns
	static constexpr uint16_t Bit1HighTicks = 16;        // 800 ns
	static constexpr uint16_t Bit1LowTicks  = 9;         // 450 ns
	static constexpr uint32_t ResetMicros   = 300;       // WS2812x latch
};

// ---------------------------------------------------------------------------
// Method implementation, compatible with the NeoPixelBus method interface.
// ---------------------------------------------------------------------------

template <typename T_SPEED>
class NeoEsp32Idf5RmtMethodBase {
	private:
		// 1 = highest ESP-IDF interrupt priority; use 2 or 3 if this conflicts with
		// another driver on your system.
		static constexpr int InterruptPriority = 1;

		const size_t _sizeData;
		const uint8_t _pin;

		RmtIdf5Driver _driver;

		uint8_t* _dataEditing = nullptr;
		uint8_t* _dataSending = nullptr;

		bool _attachErrorLogged = false;
		bool _transmitErrorLogged = false;

		void construct() {
			_dataEditing = static_cast<uint8_t*>(heap_caps_malloc(_sizeData, MALLOC_CAP_8BIT));
			_dataSending = static_cast<uint8_t*>(heap_caps_malloc(_sizeData, MALLOC_CAP_8BIT));

			if (_dataEditing) {
				memset(_dataEditing, 0, _sizeData);
			}

			if (_dataSending) {
				memset(_dataSending, 0, _sizeData);
			}
		}

	public:
		typedef NeoNoSettings SettingsObject;

		NeoEsp32Idf5RmtMethodBase(uint8_t pin, uint16_t pixelCount, size_t elementSize, size_t settingsSize) :
			_sizeData(pixelCount * elementSize + settingsSize),
			_pin(pin) {

			construct();
		}

		// Channel selection is handled internally by the new driver, so the
		// NeoBusChannel argument is accepted for signature compatibility only.
		NeoEsp32Idf5RmtMethodBase(uint8_t pin, uint16_t pixelCount, size_t elementSize, size_t settingsSize, NeoBusChannel) :
			_sizeData(pixelCount * elementSize + settingsSize),
			_pin(pin) {

			construct();
		}

		~NeoEsp32Idf5RmtMethodBase() {
			if (_dataEditing) {
				free(_dataEditing);
				_dataEditing = nullptr;
			}

			if (_dataSending) {
				free(_dataSending);
				_dataSending = nullptr;
			}
		}

		bool IsReadyToUpdate() const {
			return !_driver.isBusy();
		}

		void Initialize() {
			RmtIdf5Driver::SpeedConfig speed = {
				T_SPEED::ResolutionHz,
				T_SPEED::Bit0HighTicks,
				T_SPEED::Bit0LowTicks,
				T_SPEED::Bit1HighTicks,
				T_SPEED::Bit1LowTicks
			};

			if (!_driver.begin(_pin, speed, InterruptPriority)) {
				Serial.printf("NeoPixelBusIdf5Rmt: failed to reserve RMT channel slot for pin %u\n", static_cast<unsigned>(_pin));
			}
		}

		void Update(bool /*maintainBufferConsistency*/) {
			// Make sure the shared RMT channel is currently driving our pin. This
			// is a no-op while this strip still owns the channel; otherwise it
			// waits for the previous owner's frame and re-points the GPIO.
			if (!_driver.attach()) {
				if (!_attachErrorLogged) {
					_attachErrorLogged = true;
					Serial.printf("NeoPixelBusIdf5Rmt: failed to bind RMT channel to pin %u\n", static_cast<unsigned>(_pin));
				}
				return;
			}

			// Wait until the previous frame has left the wire, then honour the
			// latch/reset time with the line held low (eot_level = 0).
			_driver.waitAllDone(-1);
			delayMicroseconds(T_SPEED::ResetMicros);

			// Copy into the send buffer so the caller may keep editing _dataEditing
			// while the hardware streams (rmt_transmit requires a stable payload).
			memcpy(_dataSending, _dataEditing, _sizeData);

			if (!_driver.transmit(_dataSending, _sizeData)) {
				if (!_transmitErrorLogged) {
					_transmitErrorLogged = true;
					Serial.println("NeoPixelBusIdf5Rmt: transmit failed");
				}
			}
		}

		bool AlwaysUpdate() {
			// Only transmit when the buffer has been marked dirty.
			return false;
		}

		bool SwapBuffers() {
			return false;
		}

		uint8_t* getData() const {
			return _dataEditing;
		}

		size_t getDataSize() const {
			return _sizeData;
		}

		void applySettings([[maybe_unused]] const SettingsObject& settings) {
			// unused
		}
};

// Pre-built methods matching the common timing families.
typedef NeoEsp32Idf5RmtMethodBase<NeoEsp32Idf5RmtSk6812Speed> NeoEsp32Idf5RmtSk6812Method;
typedef NeoEsp32Idf5RmtMethodBase<NeoEsp32Idf5RmtWs2812xSpeed> NeoEsp32Idf5RmtWs2812xMethod;
typedef NeoEsp32Idf5RmtMethodBase<NeoEsp32Idf5RmtWs2812xSpeed> NeoEsp32Idf5Rmt800KbpsMethod;

#elif defined(ARDUINO_ARCH_ESP32)
#error "NeoPixelBusIdf5Rmt requires ESP-IDF 5.x or newer (the new RMT driver)."
#endif
