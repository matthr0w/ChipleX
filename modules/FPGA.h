#pragma once

#include <systemc>

#include "Bus.h"
#include "Cache.h"
#include "Core.h"
#include "Interconnect.h"
#include "InterconnectProtocol.h"
#include "MemoryController.h"
#include "RAM.h"

#include "common/AXIUtils.h"
#include "common/Tracker.h"

#include "include/globals.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(FPGA) {
private:
  const unsigned int fpga_id;

  // -------------------------------------------------------
  // configs
  // -------------------------------------------------------
  const Config &fpga_config = ConfigRegistry::instance().get("FPGA");
  const Config &interconnect_config =
      ConfigRegistry::instance().get("Interconnect");

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int num_cores = 1;
  const unsigned int num_bus_managers = 2;
  const unsigned int num_bus_subordinates = 2;
  const unsigned int num_interconnects = num_chiplets;

  // FPGA config
  const sc_time cores_clk_cycle = fpga_config.get<sc_time>("cores.clk_cycle");

  const unsigned int cache_size = fpga_config.get<unsigned int>("cache.size");
  const unsigned int cache_block_size =
      fpga_config.get<unsigned int>("cache.block_size");
  const unsigned int cache_store_buffer_size =
      fpga_config.get<unsigned int>("cache.store_buffer_size");
  const sc_time cache_arbitration_delay =
      fpga_config.get<sc_time>("cache.arbitration_delay");
  const sc_time cache_access_delay =
      fpga_config.get<sc_time>("cache.access_delay");

  const unsigned int axi_width = fpga_config.get<unsigned int>("axi.width");
  const sc_time axi_clk_cycle = fpga_config.get<sc_time>("axi.clk_cycle");
  const sc_time axi_arbitration_delay =
      fpga_config.get<sc_time>("axi.arbitration_delay");

  const unsigned int bus_width = fpga_config.get<unsigned int>("bus.width");
  const sc_time bus_clk_cycle = fpga_config.get<sc_time>("bus.clk_cycle");
  const sc_time bus_arbitration_delay =
      fpga_config.get<sc_time>("bus.arbitration_delay");

  const sc_time memory_controller_address_delay =
      fpga_config.get<sc_time>("memory_controller.address_delay");

  const unsigned int ram_size = fpga_config.get<unsigned int>("ram.size");
  const unsigned int ram_width = fpga_config.get<unsigned int>("ram.width");
  const sc_time ram_clk_cycle = fpga_config.get<sc_time>("ram.clk_cycle");
  const sc_time ram_access_delay = fpga_config.get<sc_time>("ram.access_delay");

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

  Core core;
  Cache cache;
  std::vector<Interconnect *> interconnects;
  RAM ram;

  SC_CTOR(FPGA);
  ~FPGA();

private:
  // AXI
  AXIUtils axi_utils;
  
  Bus bus;
  InterconnectProtocol interconnect_protocol;
  MemoryController memory_controller;

  void initialize();
};