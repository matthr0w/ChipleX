#pragma once

#include <systemc>

#include "modules/AXIBus.h"
#include "modules/Core.h"
#include "modules/Interconnect.h"
#include "modules/Memory.h"

#include "common/Tracker.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Chiplet) {
private:
  static unsigned int instance;
  const unsigned int chiplet_id;

  // -------------------------------------------------------
  // configs
  // -------------------------------------------------------
  const Config &chiplet_config = ConfigRegistry::instance().get("Chiplet");
  const Config &interconnect_config =
      ConfigRegistry::instance().get("Interconnect");

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int num_cores = 2;
  const unsigned int num_axi_managers = 2;
  const unsigned int num_axi_subordinates = 2;
  const unsigned int num_interconnects = 3;

  // chiplet config
  const unsigned int cores_clk_cycle =
      chiplet_config.get<unsigned int>("cores.clk_cycle");

  const unsigned int cache_size =
      chiplet_config.get<unsigned int>("cache.size");
  const unsigned int cache_block_size =
      chiplet_config.get<unsigned int>("cache.block_size");
  const unsigned int cache_store_buffer_size =
      chiplet_config.get<unsigned int>("cache.store_buffer_size");

  const unsigned int axi_width = chiplet_config.get<unsigned int>("axi.width");
  const unsigned int axi_clk_cycle =
      chiplet_config.get<unsigned int>("axi.clk_cycle");

  const unsigned int ram_size = chiplet_config.get<unsigned int>("ram.size");
  const unsigned int ram_clk_cycle =
      chiplet_config.get<unsigned int>("ram.clk_cycle");

  // interconnect config
  const unsigned int interconnect_flit_size =
      interconnect_config.get<unsigned int>("interconnect_protocol.flit_size");
  const unsigned int interconnect_overhead_size =
      interconnect_config.get<unsigned int>(
          "interconnect_protocol.overhead_size");
  const sc_time interconnect_pre_delay =
      interconnect_config.get<sc_time>("interconnect_protocol.pre_delay");
  const sc_time interconnect_post_delay =
      interconnect_config.get<sc_time>("interconnect_protocol.post_delay");
  const sc_time interconnect_irq_delay =
      interconnect_config.get<sc_time>("interconnect_protocol.irq_delay");
  const unsigned int interconnect_buffer_size =
      interconnect_config.get<unsigned int>("interconnect.buffer_size");
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
  Memory memory;
  // Cache cache0;
  // Cache cache1;
  Interconnect interconnect;

  SC_CTOR(Chiplet);

private:
  // AXI
  sc_clock axi_clk;
  AXIBus axi_bus;

  void initialize();
};