#include "modules/interconnects/serial_link/StreamFifo.h"

StreamFifo::StreamFifo(sc_module_name name, unsigned size)
    : sc_module(name), size(size) {}

Payload_t *StreamFifo::peek(unsigned index) {
  if (index >= data.size())
    return nullptr;
  return data[index];
}

Payload_t *StreamFifo::read() {
  Payload_t *payload = nullptr;
  if (data.size() > 0) {
    payload = data.front();
    data.pop_front();
  }
  return payload;
}

bool StreamFifo::write(Payload_t *payload) {
  if (data.size() >= size)
    return false;
  data.push_back(payload);
  return true;
}

unsigned StreamFifo::num_free() { return size - data.size(); }
unsigned StreamFifo::num_available() { return data.size(); }