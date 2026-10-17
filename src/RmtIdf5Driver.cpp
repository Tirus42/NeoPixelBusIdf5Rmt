/**
 * RmtIdf5Driver.cpp
 *
 * Per-strip handle onto the shared RMT TX channel pool. This TU intentionally
 * never includes the ESP-IDF RMT headers (RmtIdf5ChannelPool.cpp owns those).
 */

#include "RmtIdf5Driver.h"

#include "RmtIdf5ChannelPool.h"

RmtIdf5Driver::RmtIdf5Driver() :
	_pin(-1),
	_slot(-1),
	_speed{},
	_interruptPriority(1),
	_registered(false) {}

RmtIdf5Driver::~RmtIdf5Driver() {
	// The pool owns the channels and outlives every strip, so there is nothing
	// to tear down here.
}

bool RmtIdf5Driver::begin(int pin, const SpeedConfig& speed, int interruptPriority) {
	_pin = pin;
	_speed = speed;
	_interruptPriority = interruptPriority;
	_slot = RmtIdf5ChannelPool::instance().reserveSlot(pin);
	_registered = _slot >= 0;
	return _registered;
}

bool RmtIdf5Driver::attach() {
	if (!_registered) {
		return false;
	}

	return RmtIdf5ChannelPool::instance().attach(_slot, _pin, _speed, _interruptPriority);
}

bool RmtIdf5Driver::transmit(const void* data, size_t sizeBytes) {
	if (!_registered) {
		return false;
	}

	return RmtIdf5ChannelPool::instance().transmit(_slot, data, sizeBytes);
}

bool RmtIdf5Driver::waitAllDone(int timeoutMs) const {
	if (!_registered) {
		return true;
	}

	return RmtIdf5ChannelPool::instance().waitAllDone(_slot, timeoutMs);
}

bool RmtIdf5Driver::isBusy() const {
	if (!_registered) {
		return false;
	}

	return RmtIdf5ChannelPool::instance().isBusy(_slot);
}
