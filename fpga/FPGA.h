#pragma once

#include <systemc>

#include "Bus.h"
#include "Generator.h"
#include "Interconnect.h"
#include "InterconnectProtocol.h"
#include "MemoryController.h"
#include "RAM.h"

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
  fpga::Generator generator;
  std::vector<fpga::Interconnect *> interconnects;

  SC_CTOR(FPGA);
  ~FPGA();

private:
  fpga::Bus bus;
  fpga::InterconnectProtocol interconnectprotocol;
  fpga::MemoryController memorycontroller;
  fpga::RAM ram;

  void initialize();
};