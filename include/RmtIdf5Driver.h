#pragma once

#include <cstddef>
#include <cstdint>

#include "RmtSpeedConfig.h"

/**
 * RmtIdf5Driver.h
 *
 * A thin, non-template, per-strip handle onto the shared ESP-IDF "new"
 * (driver_ng) RMT TX channel pool (see RmtIdf5ChannelPool.h).
 *
 * Historically this class owned a private RMT channel. It now reserves a slot
 * in the shared pool instead, so that a board with more LED strips than the chip
 * has TX channels still works (the pool re-points a shared channel's GPIO when
 * needed). With at most `capacity()` strips the behaviour is unchanged: every
 * strip gets its own channel and nothing is ever re-pointed.
 *
 * IMPORTANT: this header deliberately contains NO ESP-IDF includes. The
 * implementation lives in RmtIdf5Driver.cpp, which only talks to the pool. The
 * new RMT headers are pulled in exclusively by RmtIdf5ChannelPool.cpp.
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
 * headers. This class hides all of that behind the pool.
 */
class RmtIdf5Driver {
	private:
		int _pin;
		int _slot;
		RmtSpeedConfig _speed;
		int _interruptPriority;
		bool _registered;

	public:
		using SpeedConfig = RmtSpeedConfig;

		RmtIdf5Driver();
		~RmtIdf5Driver();

		RmtIdf5Driver(const RmtIdf5Driver&) = delete;
		RmtIdf5Driver& operator=(const RmtIdf5Driver&) = delete;

		// Reserve a home channel slot for `pin` and remember the timing. The
		// channel itself is created lazily on the first attach().
		bool begin(int pin, const SpeedConfig& speed, int interruptPriority);

		// Ensure the reserved slot is currently driving our pin, creating or
		// re-pointing the channel as needed. Call this before transmit().
		bool attach();

		// Queue a frame. Non-blocking; `data` must stay valid until the transfer
		// completes. Returns false if the slot is not attached/created yet.
		bool transmit(const void* data, size_t sizeBytes);

		// Block (timeoutMs < 0 => forever) until the channel is idle. Returns true
		// when idle.
		bool waitAllDone(int timeoutMs) const;

		// True while a frame is still being shifted out.
		bool isBusy() const;

		// The GPIO this strip is wired to.
		int pin() const {
			return _pin;
		}

		// The pool slot this strip shares.
		int slot() const {
			return _slot;
		}
};
