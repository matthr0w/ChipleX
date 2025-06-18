#pragma once

#include <systemc>

#include "Bus.h"
#include "Cache.h"
#include "Generator.h"
#include "Interconnect.h"
#include "InterconnectProtocol.h"
#include "MemoryController.h"
#include "RAM.h"

#include "common/Tracker.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(FPGA) {
private:
  // -------------------------------------------------------
  // config
  // -------------------------------------------------------
  const Config &interconnect_config =
      ConfigRegistry::instance().get("Interconnect");

  const double bandwidth_fpga =
      interconnect_config.get<double>("interconnect.bandwidth_fpga");

public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  std::vector<BufferUsageTracker *> buffer_trackers;
  std::vector<UtilizationTracker *> utilization_trackers;

  fpga::Generator generator;
  fpga::Cache cache;
  std::vector<fpga::Interconnect *> interconnects;
  fpga::RAM ram;

  SC_CTOR(FPGA);
  ~FPGA();

private:
  fpga::Bus bus;
  fpga::InterconnectProtocol interconnectprotocol;
  fpga::MemoryController memorycontroller;

  void initialize();
};