#pragma once

#include <systemc>

#include "AXIInterconnect.h"
#include "AXIManager.h"
#include "AXISubordinate.h"
#include "Cache.h"
#include "Core.h"
#include "Interconnect.h"
#include "InterconnectProtocol.h"
#include "MemoryController.h"
#include "RAM.h"

#include "common/AXIUtils.h"
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
  const unsigned int num_axi_managers = 3;
  const unsigned int num_axi_subordinates = 2;
  const unsigned int num_interconnects = 3;

  // chiplet config
  const sc_time cores_clk_cycle =
      chiplet_config.get<sc_time>("cores.clk_cycle");

  const unsigned int cache_size =
      chiplet_config.get<unsigned int>("cache.size");
  const unsigned int cache_block_size =
      chiplet_config.get<unsigned int>("cache.block_size");
  const unsigned int cache_store_buffer_size =
      chiplet_config.get<unsigned int>("cache.store_buffer_size");
  const sc_time cache_arbitration_delay =
      chiplet_config.get<sc_time>("cache.arbitration_delay");
  const sc_time cache_access_delay =
      chiplet_config.get<sc_time>("cache.access_delay");

  const unsigned int axi_width = chiplet_config.get<unsigned int>("axi.width");
  const sc_time axi_clk_cycle = chiplet_config.get<sc_time>("axi.clk_cycle");
  const sc_time axi_arbitration_delay =
      chiplet_config.get<sc_time>("axi.arbitration_delay");

  const unsigned int bus_width = chiplet_config.get<unsigned int>("bus.width");
  const sc_time bus_clk_cycle = chiplet_config.get<sc_time>("bus.clk_cycle");
  const sc_time bus_arbitration_delay =
      chiplet_config.get<sc_time>("bus.arbitration_delay");

  const sc_time memory_controller_address_delay =
      chiplet_config.get<sc_time>("memory_controller.address_delay");

  const unsigned int ram_size = chiplet_config.get<unsigned int>("ram.size");
  const unsigned int ram_width = chiplet_config.get<unsigned int>("ram.width");
  const sc_time ram_clk_cycle = chiplet_config.get<sc_time>("ram.clk_cycle");
  const sc_time ram_access_delay =
      chiplet_config.get<sc_time>("ram.access_delay");

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
  Cache cache0;
  Cache cache1;
  RAM ram;
  std::vector<Interconnect *> interconnects;

  SC_CTOR(Chiplet);
  ~Chiplet();

private:
  // AXI
  AXIUtils axi_utils;
  AXIInterconnect axi_interconnect;
  AXIManager axi_manager_core0;
  AXIManager axi_manager_core1;
  AXIManager axi_manager_interconnect;
  AXISubordinate axi_subordinate_interconnect;
  AXISubordinate axi_subordinate_memory_controller;

  InterconnectProtocol interconnect_protocol;
  MemoryController memory_controller;

  void initialize();
};