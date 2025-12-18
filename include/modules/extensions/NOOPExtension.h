#pragma once

#include "logging.h"

#include "modules/extensions/ExtensionBase.h"
#include "modules/extensions/ExtensionIDs.h"

class NOOPExtension : public ExtensionBase {
public:
  uint8_t id() const override { return SmartExtension::NOOP; }

  bool can_accept() const override { return fifo.size() < 16; }

  void push(const AxiBeat &b) override {
    LOG_INFO("NOOP EXTENSION PUSH");
    fifo.push_back(b);
  }

  bool has_output() const override { return !fifo.empty(); }

  AxiBeat peek() const override { return fifo.front(); }

  AxiBeat pop() override {
    AxiBeat beat = fifo.front();
    fifo.pop_front();
    return beat;
  }

private:
  std::deque<AxiBeat> fifo;
};