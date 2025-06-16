#pragma once

#include <systemc>

#include "Bus.h"
#include "Cache.h"
#include "Core.h"
#include "Interconnect.h"
#include "InterconnectProtocol.h"
#include "MemoryController.h"
#include "RAM.h"

#include "common/Tracker.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Chiplet) {
private:
  static unsigned int instance;
  const unsigned int chiplet_id;

  // -------------------------------------------------------
  // config
  // -------------------------------------------------------
  const Config &interconnect_config =
      ConfigRegistry::instance().get("Interconnect");

  const double bandwidth_fpga =
      interconnect_config.get<double>("interconnect.bandwidth_fpga");
  const double bandwidth_chiplet =
      interconnect_config.get<double>("interconnect.bandwidth_chiplets");

public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  std::vector<BufferUsageTracker *> buffer_trackers;
  std::vector<UtilizationTracker *> utilization_trackers;

  chiplet::Core core0;
  chiplet::Core core1;
  chiplet::Cache cache0;
  chiplet::Cache cache1;
  std::vector<chiplet::Interconnect *> interconnects;
  chiplet::RAM ram;

  SC_CTOR(Chiplet);
  ~Chiplet();

private:
  chiplet::Bus bus;
  chiplet::InterconnectProtocol interconnectprotocol;
  chiplet::MemoryController memorycontroller;

  void initialize();
};