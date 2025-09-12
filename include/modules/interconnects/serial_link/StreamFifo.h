#pragma once

#include <deque>

#include "modules/interconnects/serial_link/FifoIf.h"
#include "modules/interconnects/serial_link/Types.h"

SC_MODULE(StreamFifo), public FifoIf {
public:
  StreamFifo(sc_module_name name, unsigned size);

  Payload_t *peek(unsigned index) override;
  Payload_t *read() override;
  bool write(Payload_t * payload) override;

  unsigned num_free() override;
  unsigned num_available() override;

private:
  const unsigned size;
  std::deque<Payload_t *> data;
};