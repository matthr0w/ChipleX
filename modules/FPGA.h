#pragma once

#include <systemc>

#include "AXIBus.h"
#include "Core.h"
#include "Memory.h"

#include "common/Tracker.h"

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
  const unsigned int num_axi_managers = 1;
  const unsigned int num_axi_subordinates = 1;
  const unsigned int num_interconnects = num_chiplets;

  // FPGA config
  const unsigned int cores_clk_cycle =
      fpga_config.get<unsigned int>("cores.clk_cycle");

  const unsigned int cache_size = fpga_config.get<unsigned int>("cache.size");
  const unsigned int cache_block_size =
      fpga_config.get<unsigned int>("cache.block_size");
  const unsigned int cache_store_buffer_size =
      fpga_config.get<unsigned int>("cache.store_buffer_size");

  const unsigned int axi_width = fpga_config.get<unsigned int>("axi.width");
  const unsigned int axi_clk_cycle =
      fpga_config.get<unsigned int>("axi.clk_cycle");

  const unsigned int ram_size = fpga_config.get<unsigned int>("ram.size");
  const unsigned int ram_clk_cycle =
      fpga_config.get<unsigned int>("ram.clk_cycle");

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
  Memory memory;
  // Cache cache;
  // std::vector<Interconnect *> interconnects;

  SC_CTOR(FPGA);
  ~FPGA();

private:
  // AXI
  sc_clock axi_clk;
  AXIBus axi_bus;

  // InterconnectProtocol interconnect_protocol;

  void initialize();
};