#pragma once

#include <cstdint>

#include "ARM/TLM/arm_axi4.h"

enum class AxiDir : uint8_t { TO_BUS, TO_IC };

struct AxiBeat {
  ARM::AXI::Payload *payload;
  ARM::AXI::Phase phase;
  AxiDir dir;
};

class ExtensionBase {
public:
  virtual ~ExtensionBase() = default;

  virtual uint8_t id() const = 0;

  virtual bool can_accept() const = 0;
  virtual void push(const AxiBeat &beat) = 0;

  virtual bool has_output() const = 0;
  virtual AxiBeat peek() const = 0;
  virtual AxiBeat pop() = 0;

  virtual void tick() {}
};