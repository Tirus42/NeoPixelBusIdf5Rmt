#pragma once

#include <cstddef>
#include <cstdint>

/**
 * RmtIdf5Driver.h
 *
 * A thin, non-template wrapper around a single ESP-IDF "new" (driver_ng) RMT
 * TX channel.
 *
 * IMPORTANT: this header deliberately contains NO ESP-IDF includes. The
 * implementation lives in RmtIdf5Driver.cpp, which is the only translation unit
 * that pulls in driver/rmt_tx.h.
 *
 * Why the split:
 * NeoPixelBus.h transitively includes the *deprecated* driver/rmt.h, which
 * defines the legacy `rmt_channel_t`. The *new* driver/rmt_types.h declares
 * `typedef struct rmt_channel_t *rmt_channel_handle_t;`. Having both in the
 * same translation unit fails to compile with:
 *
 *     error: using typedef-name 'rmt_channel_t' after 'struct'
 *
 * Any TU that includes NeoPixelBus.h therefore must not include the new RMT
 * headers. This class hides all of that behind `void*`.
 */
class RmtIdf5Driver {
	private:
		void* _channel; // rmt_channel_handle_t
		void* _encoder; // rmt_encoder_handle_t

	public:
		struct SpeedConfig {
			uint32_t resolutionHz; // RMT tick rate, e.g. 20000000 for 50 ns/tick
			uint16_t bit0High;     // bit 0, high level, in ticks
			uint16_t bit0Low;      // bit 0, low level, in ticks
			uint16_t bit1High;     // bit 1, high level, in ticks
			uint16_t bit1Low;      // bit 1, low level, in ticks
		};

		RmtIdf5Driver();
		~RmtIdf5Driver();

		RmtIdf5Driver(const RmtIdf5Driver&) = delete;
		RmtIdf5Driver& operator=(const RmtIdf5Driver&) = delete;

		// Create + enable the channel and byte encoder. Returns false on failure.
		bool begin(int pin, const SpeedConfig& speed, int interruptPriority);

		// Queue a frame. Non-blocking; `data` must stay valid until the transfer
		// completes. Returns false if the driver is not ready.
		bool transmit(const void* data, size_t sizeBytes);

		// Block (timeoutMs < 0 => forever) until the channel is idle. Returns true
		// when idle.
		bool waitAllDone(int timeoutMs) const;

		// True while a frame is still being shifted out.
		bool isBusy() const;
};
