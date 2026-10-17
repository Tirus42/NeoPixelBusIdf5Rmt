/**
 * RmtIdf5Driver.cpp
 *
 * The ONLY translation unit that includes the ESP-IDF "new" RMT driver.
 * It must never include <NeoPixelBus.h> (see RmtIdf5Driver.h for the reason).
 */

#include "RmtIdf5Driver.h"

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "soc/soc_caps.h"

RmtIdf5Driver::RmtIdf5Driver() :
	_channel(nullptr),
	_encoder(nullptr) {}

RmtIdf5Driver::~RmtIdf5Driver() {
	if (_channel)
	{
		rmt_disable(static_cast<rmt_channel_handle_t>(_channel));
	}

	if (_encoder) {
		rmt_del_encoder(static_cast<rmt_encoder_handle_t>(_encoder));
		_encoder = nullptr;
	}

	if (_channel) {
		rmt_del_channel(static_cast<rmt_channel_handle_t>(_channel));
		_channel = nullptr;
	}
}

bool RmtIdf5Driver::begin(int pin, const SpeedConfig& speed, int interruptPriority) {
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
	_channel = channel;

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
		return false;
	}
	_encoder = encoder;

	if (rmt_enable(channel) != ESP_OK) {
		return false;
	}

	return true;
}

bool RmtIdf5Driver::transmit(const void* data, size_t sizeBytes) {
	if (!_channel || !_encoder) {
		return false;
	}

	rmt_transmit_config_t txConfig = {};
	txConfig.loop_count = 0;
	txConfig.flags.eot_level = 0; // hold the line low between frames
	txConfig.flags.queue_nonblocking = 0;

	return rmt_transmit(static_cast<rmt_channel_handle_t>(_channel),
						static_cast<rmt_encoder_handle_t>(_encoder),
						data,
						sizeBytes,
						&txConfig) == ESP_OK;
}

bool RmtIdf5Driver::waitAllDone(int timeoutMs) const {
	if (!_channel) {
		return true;
	}

	return rmt_tx_wait_all_done(static_cast<rmt_channel_handle_t>(_channel), timeoutMs) == ESP_OK;
}

bool RmtIdf5Driver::isBusy() const {
	if (!_channel) {
		return false;
	}

	return rmt_tx_wait_all_done(static_cast<rmt_channel_handle_t>(_channel), 0) != ESP_OK;
}
