#pragma once

#include <systemc>

#include "modules/chiplets/Clocks.h"
#include "modules/interconnects/InterconnectBase.h"
#include "setup/Types.h"

using namespace sc_core;

struct ChipletBase : sc_module {
protected:
  const std::string chiplet_name;
  const unsigned chiplet_id;

  Clocks chiplet_clocks;
  Clocks interconnect_clocks;

public:
  ChipletBase(sc_module_name name, unsigned id, SystemConfig sysconf)
      : sc_module(name), chiplet_name(name), chiplet_id(id),
        chiplet_clocks(sysconf.chiplets[chiplet_name].config),
        interconnect_clocks(sysconf.interconnect.config) {}

  virtual ~ChipletBase() = default;

  std::unique_ptr<InterconnectBase> interconnect;
};