#pragma once

#include <systemc>

#include "modules/AXIBus.h"
#include "modules/Cache.h"
#include "modules/Core.h"
#include "modules/DMAEngine.h"
#include "modules/Interconnect.h"
#include "modules/Memory.h"

#include "common/Tracker.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Chiplet) {
private:
  static unsigned instance;
  const unsigned chiplet_id;

  // -------------------------------------------------------
  // configs
  // -------------------------------------------------------
  const Config &chiplet_config = ConfigRegistry::instance().get("Chiplet");
  const Config &interconnect_config =
      ConfigRegistry::instance().get("Interconnect");

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned num_cores = 2;
  const unsigned num_axi_managers = 3;
  const unsigned num_axi_subordinates = 2;
  const unsigned num_interconnects = 3;

  // chiplet config
  const unsigned cores_clk_cycle =
      chiplet_config.get<unsigned>("cores.clk_cycle");

  const unsigned cache_size = chiplet_config.get<unsigned>("cache.size");
  const unsigned cache_block_size =
      chiplet_config.get<unsigned>("cache.block_size");
  const unsigned cache_store_buffer_size =
      chiplet_config.get<unsigned>("cache.store_buffer_size");

  const unsigned axi_width = chiplet_config.get<unsigned>("axi.width");
  const unsigned axi_clk_cycle = chiplet_config.get<unsigned>("axi.clk_cycle");

  const unsigned ram_size = chiplet_config.get<unsigned>("ram.size");
  const unsigned ram_clk_cycle = chiplet_config.get<unsigned>("ram.clk_cycle");

  // interconnect config
  const unsigned interconnect_flit_size =
      interconnect_config.get<unsigned>("interconnect_protocol.flit_size");
  const unsigned interconnect_overhead_size =
      interconnect_config.get<unsigned>("interconnect_protocol.overhead_size");
  const sc_time interconnect_irq_delay =
      interconnect_config.get<sc_time>("interconnect_protocol.irq_delay");
  const unsigned interconnect_staging_buffer_size =
      interconnect_config.get<unsigned>("interconnect.staging_buffer_size");
  const unsigned interconnect_link_buffer_size =
      interconnect_config.get<unsigned>("interconnect.link_buffer_size");
  const double interconnect_bandwidth_chiplets =
      interconnect_config.get<double>("interconnect.bandwidth_chiplets");
  const double interconnect_bandwidth_fpga =
      interconnect_config.get<double>("interconnect.bandwidth_fpga");

public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  std::vector<BufferUsageTracker *> buffer_trackers;
  std::vector<UtilizationTracker *> utilization_trackers;

  Core core0;
  Core core1;
  Cache cache0;
  Cache cache1;
  Memory memory;
  DMAEngine dma_engine;
  Interconnect interconnect;

  SC_CTOR(Chiplet);

private:
  // AXI
  sc_clock axi_clk;
  AXIBus axi_bus;

  void initialize();
};