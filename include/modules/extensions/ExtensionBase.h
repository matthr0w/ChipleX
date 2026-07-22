#pragma once

#include <cstdint>

#include "ARM/TLM/arm_axi4.h"

enum class AxiDir : uint8_t {
    UPSTREAM,
    DOWNSTREAM
};

struct AxiBeat {
    ARM::AXI::Payload *payload = nullptr;
    ARM::AXI::Phase    phase   = ARM::AXI4::PHASE_UNINITIALIZED;
    AxiDir             dir     = AxiDir::UPSTREAM;
    int                index   = 0;
};

class ExtensionBase {
  public:
    ExtensionBase(unsigned axi_width) : axi_width(axi_width) {};
    virtual ~ExtensionBase() = default;

    const unsigned axi_width;

    virtual uint8_t id() const = 0;

    virtual bool can_accept() const        = 0;
    virtual void push(const AxiBeat &beat) = 0;

    virtual bool    has_output() const = 0;
    virtual AxiBeat peek() const       = 0;
    virtual AxiBeat pop()              = 0;

    virtual void tick() {}
};