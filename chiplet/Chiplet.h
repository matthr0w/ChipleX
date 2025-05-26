#pragma once

#include <systemc>

#include "Bus.h"
#include "Core.h"
#include "Interconnect.h"
#include "InterconnectProtocol.h"
#include "MemoryController.h"
#include "RAM.h"

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
  chiplet::Core core0;
  chiplet::Core core1;
  std::vector<chiplet::Interconnect *> interconnects;

  SC_CTOR(Chiplet);
  ~Chiplet();

private:
  chiplet::Bus bus;
  chiplet::InterconnectProtocol interconnectprotocol;
  chiplet::MemoryController memorycontroller;
  chiplet::RAM ram;

  void initialize();
};