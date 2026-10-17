#pragma once

#include <cstdint>

/**
 * RmtSpeedConfig.h
 *
 * Timing profile for a 1-wire LED protocol expressed as RMT tick counts.
 *
 * Shared by RmtIdf5Driver and RmtIdf5ChannelPool. Deliberately contains no
 * ESP-IDF includes so it can be pulled in next to <NeoPixelBus.h>.
 */
struct RmtSpeedConfig {
	uint32_t resolutionHz; // RMT tick rate, e.g. 20000000 for 50 ns/tick
	uint16_t bit0High;     // bit 0, high level, in ticks
	uint16_t bit0Low;      // bit 0, low level, in ticks
	uint16_t bit1High;     // bit 1, high level, in ticks
	uint16_t bit1Low;      // bit 1, low level, in ticks
};
