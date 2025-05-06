#pragma once

#include <systemc>

#include "Bus.h"
#include "Generator.h"
#include "Interconnect.h"
#include "InterconnectProtocol.h"
#include "RAM.h"

#include "include/globals.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(FPGA) {
public:
  std::vector<fpga::Interconnect *> interconnects;

  SC_CTOR(FPGA);

private:
  fpga::Bus bus;
  fpga::Generator generator;
  fpga::InterconnectProtocol interconnectprotocol;
  fpga::RAM ram;

  void initialize();
};