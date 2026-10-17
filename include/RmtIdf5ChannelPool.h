#pragma once

#include <cstddef>
#include <cstdint>

#include "RmtSpeedConfig.h"

/**
 * RmtIdf5ChannelPool.h
 *
 * A small pool of long-lived ESP-IDF "new" (driver_ng) RMT TX channels that can
 * be re-pointed at a different GPIO on demand, so several LED strips can share
 * the handful of TX channels the chip actually has.
 *
 * Why
 * ---
 * The ESP32-C3 exposes only SOC_RMT_TX_CANDIDATES_PER_GROUP (2) TX channels per
 * RMT group; ESP32-S3/C6 have 4. If a board wires more LED strips than the chip
 * has TX channels and each strip insists on owning a channel, the extra strips
 * fail to initialise (rmt_new_tx_channel -> ESP_ERR_NOT_FOUND). This pool keeps
 * a small fixed set of channels alive and shares them between strips:
 *
 *   - while the number of strips is <= capacity(), every strip reserves its own
 *     "home slot" and no pin switching ever happens (behaviour identical to
 *     giving each strip its own channel);
 *   - once there are more strips than slots, the extra strips share a slot: a
 *     slot is re-pointed (disable -> rmt_tx_switch_gpio -> enable) when its
 *     owner changes. Slots are handed out round-robin, which minimises the
 *     number of re-points for a uniform refresh order.
 *
 * Re-pointing releases the old GPIO: the IDF implementation calls
 * gpio_output_disable() + esp_gpio_revoke() on the previous pin, so that data
 * line floats until the slot comes back to it. Add external pull-downs (or
 * expect flicker) on a shared line if the strip is sensitive to that.
 *
 * Slot allocation is deliberately static/deterministic (a fixed partition of
 * pins over slots) rather than a recency cache: with a uniform "refresh every
 * strip every frame" access pattern a recency cache thrashes and re-points on
 * every strip, whereas a fixed partition only re-points for the strips that
 * share a slot.
 *
 * NOTE: no ESP-IDF includes here on purpose (this header is pulled in next to
 * <NeoPixelBus.h>, which transitively includes the legacy driver/rmt.h). The
 * implementation lives in RmtIdf5ChannelPool.cpp, the only TU that mixes in the
 * new RMT driver.
 */
class RmtIdf5ChannelPool {
	private:
		// A channel, its byte encoder, and the GPIO it currently drives.
		struct Channel {
			void* handle;  // rmt_channel_handle_t
			void* encoder; // rmt_encoder_handle_t
			int pin;       // GPIO currently driven, or -1 when not bound
		};

		// Highest slot index this pool can address.
		static constexpr int MaxSlots = 8;

		// Largest GPIO number that can be used as a pool key.
		static constexpr int MaxPins = 64;

		RmtIdf5ChannelPool();
		~RmtIdf5ChannelPool();

		RmtIdf5ChannelPool(const RmtIdf5ChannelPool&) = delete;
		RmtIdf5ChannelPool& operator=(const RmtIdf5ChannelPool&) = delete;

		bool createChannel(int slot, int pin, const RmtSpeedConfig& speed, int interruptPriority);

		Channel _channels[MaxSlots];
		int _capacity;
		int _usedSlots;
		int _cycler;
		int _pinToSlot[MaxPins];

	public:
		static RmtIdf5ChannelPool& instance();

		// Number of physical TX channels available on this chip.
		int capacity() const;

		// Reserve a stable home slot for `pin`. Slots are handed out one at a
		// time until all `capacity()` slots are taken; further pins then share
		// slots round-robin. The same pin always maps to the same slot.
		int reserveSlot(int pin);

		// Make sure `slot` is currently driving `pin` with `speed`. Creates the
		// channel on first use, otherwise re-points it. Blocks until the slot is
		// idle before re-pointing. Returns false on failure.
		bool attach(int slot, int pin, const RmtSpeedConfig& speed, int interruptPriority);

		// Queue a frame on `slot`. Non-blocking; `data` must stay valid until the
		// transfer completes. The slot must have been attached first.
		bool transmit(int slot, const void* data, size_t sizeBytes);

		// Wait (timeoutMs < 0 => forever) until `slot` is idle. Returns true when
		// idle.
		bool waitAllDone(int slot, int timeoutMs) const;

		// True while `slot` is still shifting out a frame.
		bool isBusy(int slot) const;

		// Diagnostics: the GPIO a slot currently drives (-1 when unbound).
		int slotPin(int slot) const;
};
