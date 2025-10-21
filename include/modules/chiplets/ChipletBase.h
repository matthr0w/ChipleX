#pragma once

#include <systemc>

#include "modules/interconnects/InterconnectBase.h"
#include "setup/Types.h"

using namespace sc_core;

struct ChipletBase : sc_module {
protected:
  const std::string chiplet_name;
  const unsigned chiplet_id;
  sc_clock system_clk;

public:
  ChipletBase(sc_module_name name, unsigned id, SystemConfig sysconf)
      : sc_module(name), chiplet_name(name), chiplet_id(id),
        system_clk("system_clk",
                   sysconf.chiplets[chiplet_name]
                       .config["system"]["clk_cycle"]
                       .as<int>(),
                   SC_NS, 0.5) {}

  virtual ~ChipletBase() = default;

  std::unique_ptr<InterconnectBase> interconnect;
};