#include "modules/interconnects/serial_link/StreamFifo.h"

StreamFifo::StreamFifo(sc_module_name name, unsigned size) : sc_module(name), size(size) {}

Payload_t *StreamFifo::peek(unsigned index) {
	if (index >= data.size()) {
		return nullptr;
	}
	return data[index];
}

Payload_t *StreamFifo::read() {
	if (data.empty()) {
		return nullptr;
	}
	Payload_t *payload = data.front();
	data.pop_front();
	fill_level--;
	read_event.notify(SC_ZERO_TIME);
	return payload;
}

bool StreamFifo::write(Payload_t *payload) {
	if (reserved > 0) {
		reserved--;
	} else {
		fill_level++;
	}
	data.push_back(payload);
	write_event.notify(SC_ZERO_TIME);
	return true;
}

bool StreamFifo::reserve() {
	if (fill_level >= size) {
		return false;
	}
	fill_level++;
	reserved++;
	return true;
}

unsigned StreamFifo::num_free() {
	// Guard against unsigned underflow if fill_level ever exceeds size.
	return fill_level >= size ? 0 : size - fill_level;
}

unsigned StreamFifo::num_available() {
	return data.size();
}