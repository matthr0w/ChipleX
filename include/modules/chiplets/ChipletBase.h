#pragma once

#include <systemc>

#include "modules/chiplets/ChipletDescriptor.h"
#include "modules/chiplets/ChipletRegistry.h"
#include "modules/chiplets/Clocks.h"
#include "modules/interconnects/InterconnectBase.h"
#include "setup/Types.h"

using namespace sc_core;

struct ChipletBase : sc_module {
protected:
  const unsigned chiplet_id;
  const std::string chiplet_name;

  Clocks chiplet_clocks;
  Clocks interconnect_clocks;

  const ChipletDescriptor chiplet_desc;

public:
  ChipletBase(sc_module_name name, const ChipletDescriptor &desc,
              SystemConfig sysconf)
      : sc_module(name), chiplet_id(desc.chiplet_id),
        chiplet_name(desc.chiplet_name), chiplet_desc(desc),
        chiplet_clocks(sysconf.chiplets[chiplet_name].config),
        interconnect_clocks(sysconf.interconnect.config) {
    ChipletRegistry::instance().register_chiplet(chiplet_id, chiplet_name,
                                                 chiplet_desc);
  }

  virtual ~ChipletBase() = default;

  std::unique_ptr<InterconnectBase> interconnect;
};