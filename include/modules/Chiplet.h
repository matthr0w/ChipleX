#pragma once

#include <systemc>

#include "modules/AXIBus.h"
#include "modules/Cache.h"
#include "modules/Core.h"
#include "modules/DMAEngine.h"
#include "modules/Memory.h"
#include "modules/interconnects/Base.h"

#include "common/Tracker.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Chiplet) {
private:
  static unsigned instance;
  const unsigned chiplet_id;

  // -------------------------------------------------------
  // config
  // -------------------------------------------------------
  const Config &config = ConfigRegistry::instance().get("Chiplet");

  const unsigned cores_clk_cycle = config.get<unsigned>("cores.clk_cycle");
  const sc_time cores_irq_delay = config.get<sc_time>("cores.irq_delay");

  const unsigned cache_size = config.get<unsigned>("cache.size");
  const unsigned cache_block_size = config.get<unsigned>("cache.block_size");
  const unsigned cache_store_buffer_size =
      config.get<unsigned>("cache.store_buffer_size");

  const unsigned axi_width = config.get<unsigned>("axi.width");
  const unsigned axi_clk_cycle = config.get<unsigned>("axi.clk_cycle");

  const unsigned ram_size = config.get<unsigned>("ram.size");
  const unsigned ram_clk_cycle = config.get<unsigned>("ram.clk_cycle");

public:
  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned num_cores = 2;
  const unsigned num_axi_managers = 3;
  const unsigned num_axi_subordinates = 2;
  const unsigned num_interconnects = 3;

  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  std::vector<BufferUsageTracker *> buffer_trackers;
  std::vector<UtilizationTracker *> utilization_trackers;

  Memory memory;
  DMAEngine dma_engine;
  std::vector<std::unique_ptr<Core>> cores;
  std::vector<std::unique_ptr<Cache>> caches;
  std::unique_ptr<InterconnectBase> interconnect;

  SC_CTOR(Chiplet);

private:
  // AXI
  sc_clock axi_clk;
  AXIBus axi_bus;
};