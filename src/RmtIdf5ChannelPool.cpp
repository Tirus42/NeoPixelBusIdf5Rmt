/**
 * RmtIdf5ChannelPool.cpp
 *
 * The pool of shareable ESP-IDF "new" (driver_ng) RMT TX channels.
 *
 * This is the only translation unit that mixes the new RMT driver with the rest
 * of the library; it must never include <NeoPixelBus.h> (see RmtIdf5ChannelPool.h
 * for the reason).
 *
 * Set RMT_IDF5_POOL_LOG_SWITCHES to 1 to printf() every GPIO re-point. Handy
 * while bringing up a board, but far too chatty to leave on (a shared slot is
 * re-pointed once or twice per refresh).
 */

#include "RmtIdf5ChannelPool.h"

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "soc/soc_caps.h"

#include <cstdio>

#ifndef RMT_IDF5_POOL_LOG_SWITCHES
#define RMT_IDF5_POOL_LOG_SWITCHES 0
#endif

#ifndef SOC_RMT_TX_CANDIDATES_PER_GROUP
#define SOC_RMT_TX_CANDIDATES_PER_GROUP 1
#endif

RmtIdf5ChannelPool& RmtIdf5ChannelPool::instance() {
	static RmtIdf5ChannelPool pool;
	return pool;
}

RmtIdf5ChannelPool::RmtIdf5ChannelPool() :
	_capacity(0),
	_usedSlots(0),
	_cycler(0) {
	_capacity = SOC_RMT_TX_CANDIDATES_PER_GROUP;
	if (_capacity < 1) {
		_capacity = 1;
	}
	if (_capacity > MaxSlots) {
		_capacity = MaxSlots;
	}

	for (int i = 0; i < MaxSlots; ++i) {
		_channels[i].handle = nullptr;
		_channels[i].encoder = nullptr;
		_channels[i].pin = -1;
	}

	for (int i = 0; i < MaxPins; ++i) {
		_pinToSlot[i] = -1;
	}
}

RmtIdf5ChannelPool::~RmtIdf5ChannelPool() {
	for (int i = 0; i < MaxSlots; ++i) {
		Channel& c = _channels[i];

		if (c.handle) {
			rmt_disable(static_cast<rmt_channel_handle_t>(c.handle));
		}

		if (c.encoder) {
			rmt_del_encoder(static_cast<rmt_encoder_handle_t>(c.encoder));
			c.encoder = nullptr;
		}

		if (c.handle) {
			rmt_del_channel(static_cast<rmt_channel_handle_t>(c.handle));
			c.handle = nullptr;
		}
	}
}

int RmtIdf5ChannelPool::capacity() const {
	return _capacity;
}

int RmtIdf5ChannelPool::reserveSlot(int pin) {
	if (pin >= 0 && pin < MaxPins && _pinToSlot[pin] >= 0) {
		return _pinToSlot[pin];
	}

	int slot;
	if (_usedSlots < _capacity) {
		// Still have a channel that has never been handed out: give this pin
		// its own slot, so it will never be re-pointed.
		slot = _usedSlots;
		++_usedSlots;
	} else {
		// More strips than channels: share, one slot at a time.
		slot = _cycler;
		_cycler = (_cycler + 1) % _capacity;
	}

	if (pin >= 0 && pin < MaxPins) {
		_pinToSlot[pin] = slot;
	}

	return slot;
}

bool RmtIdf5ChannelPool::attach(int slot, int pin, const RmtSpeedConfig& speed, int interruptPriority) {
	if (slot < 0 || slot >= MaxSlots) {
		return false;
	}

	Channel& c = _channels[slot];

	if (!c.handle) {
		return createChannel(slot, pin, speed, interruptPriority);
	}

	if (c.pin == pin) {
		return true; // already driving this pin
	}

	// We must wait for the shared channel to finish whatever it is sending for
	// its current owner before we can safely re-point it.
	rmt_tx_wait_all_done(static_cast<rmt_channel_handle_t>(c.handle), -1);

#if RMT_IDF5_POOL_LOG_SWITCHES
	printf("RmtIdf5ChannelPool: slot %d: pin %d -> pin %d\n", slot, c.pin, pin);
#endif

	if (rmt_disable(static_cast<rmt_channel_handle_t>(c.handle)) != ESP_OK) {
		return false;
	}

	// rmt_tx_switch_gpio() only works while the channel is disabled (INIT
	// state). It releases the old GPIO (which is left floating) and routes the
	// RMT signal to the new one.
	if (rmt_tx_switch_gpio(static_cast<rmt_channel_handle_t>(c.handle),
						   static_cast<gpio_num_t>(pin),
						   false) != ESP_OK) {
		// Try to leave the channel in a usable state even if the switch failed.
		rmt_enable(static_cast<rmt_channel_handle_t>(c.handle));
		return false;
	}

	if (rmt_enable(static_cast<rmt_channel_handle_t>(c.handle)) != ESP_OK) {
		return false;
	}

	c.pin = pin;
	return true;
}

bool RmtIdf5ChannelPool::createChannel(int slot, int pin, const RmtSpeedConfig& speed, int interruptPriority) {
	Channel& c = _channels[slot];

	rmt_tx_channel_config_t config = {};
	config.clk_src = RMT_CLK_SRC_DEFAULT;
	config.gpio_num = static_cast<gpio_num_t>(pin);
	// Non-DMA path: the per-channel RMT RAM (48 symbols on ESP32-C3).
	config.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
	config.resolution_hz = speed.resolutionHz;
	config.trans_queue_depth = 4;
	// 0 = driver default (low/medium); a high level keeps the refill ISR from
	// being preempted by BLE or other drivers.
	config.intr_priority = interruptPriority;
	config.flags.invert_out = false;
	config.flags.with_dma = false; // ESP32-C3 has no RMT DMA
	config.flags.io_loop_back = false;
	config.flags.io_od_mode = false;
	config.flags.allow_pd = false;

	rmt_channel_handle_t channel = nullptr;
	if (rmt_new_tx_channel(&config, &channel) != ESP_OK) {
		return false;
	}

	rmt_bytes_encoder_config_t bytesConfig = {};
	bytesConfig.bit0.level0 = 1;
	bytesConfig.bit0.duration0 = speed.bit0High;
	bytesConfig.bit0.level1 = 0;
	bytesConfig.bit0.duration1 = speed.bit0Low;
	bytesConfig.bit1.level0 = 1;
	bytesConfig.bit1.duration0 = speed.bit1High;
	bytesConfig.bit1.level1 = 0;
	bytesConfig.bit1.duration1 = speed.bit1Low;
	bytesConfig.flags.msb_first = 1;

	rmt_encoder_handle_t encoder = nullptr;
	if (rmt_new_bytes_encoder(&bytesConfig, &encoder) != ESP_OK) {
		rmt_del_channel(channel);
		return false;
	}

	if (rmt_enable(channel) != ESP_OK) {
		rmt_del_encoder(encoder);
		rmt_del_channel(channel);
		return false;
	}

	c.handle = channel;
	c.encoder = encoder;
	c.pin = pin;
	return true;
}

bool RmtIdf5ChannelPool::transmit(int slot, const void* data, size_t sizeBytes) {
	if (slot < 0 || slot >= MaxSlots) {
		return false;
	}

	Channel& c = _channels[slot];
	if (!c.handle || !c.encoder) {
		return false;
	}

	rmt_transmit_config_t txConfig = {};
	txConfig.loop_count = 0;
	txConfig.flags.eot_level = 0; // hold the line low between frames
	txConfig.flags.queue_nonblocking = 0;

	return rmt_transmit(static_cast<rmt_channel_handle_t>(c.handle),
						static_cast<rmt_encoder_handle_t>(c.encoder),
						data,
						sizeBytes,
						&txConfig) == ESP_OK;
}

bool RmtIdf5ChannelPool::waitAllDone(int slot, int timeoutMs) const {
	if (slot < 0 || slot >= MaxSlots) {
		return true;
	}

	const Channel& c = _channels[slot];
	if (!c.handle) {
		return true;
	}

	return rmt_tx_wait_all_done(static_cast<rmt_channel_handle_t>(c.handle), timeoutMs) == ESP_OK;
}

bool RmtIdf5ChannelPool::isBusy(int slot) const {
	if (slot < 0 || slot >= MaxSlots) {
		return false;
	}

	const Channel& c = _channels[slot];
	if (!c.handle) {
		return false;
	}

	return rmt_tx_wait_all_done(static_cast<rmt_channel_handle_t>(c.handle), 0) != ESP_OK;
}

int RmtIdf5ChannelPool::slotPin(int slot) const {
	if (slot < 0 || slot >= MaxSlots) {
		return -1;
	}

	return _channels[slot].pin;
}
