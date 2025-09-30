#pragma once

#include <systemc>

#include "common/System.h"

#include "modules/AXIBus.h"
#include "modules/Cache.h"
#include "modules/Core.h"
#include "modules/DMAEngine.h"
#include "modules/Memory.h"
#include "modules/interconnects/Base.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Chiplet) {
private:
  static unsigned instance;
  const unsigned chiplet_id;

  const ChipletConfig chiplet_config;
  const InterconnectConfig interconnect_config;

  unsigned init_num_managers();
  unsigned init_num_subordinates();

public:
  const unsigned num_cores;
  const unsigned num_managers;
  const unsigned num_subordinates;

  AXIBus axi_bus;
  DMAEngine dma_engine;
  Memory memory;
  std::vector<std::unique_ptr<Core>> cores;
  std::vector<std::unique_ptr<Cache>> caches;
  std::unique_ptr<InterconnectBase> interconnect;

  Chiplet(sc_module_name name, SystemConfig sysconf);

private:
  sc_clock system_clk;
};