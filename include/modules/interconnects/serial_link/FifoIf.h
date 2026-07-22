#pragma once

#include <systemc>

#include "modules/interconnects/serial_link/Types.h"

using namespace sc_core;

class FifoIf : public sc_interface {
  public:
    virtual Payload_t *peek(unsigned index = 0)  = 0;
    virtual Payload_t *read()                    = 0;
    virtual bool       write(Payload_t *payload) = 0;
    virtual bool       reserve()                 = 0;

    virtual unsigned num_free()      = 0;
    virtual unsigned num_available() = 0;

    sc_event read_event;
    sc_event write_event;
};