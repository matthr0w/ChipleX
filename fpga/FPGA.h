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
  fpga::Generator generator;
  std::vector<fpga::Interconnect *> interconnects;

  SC_CTOR(FPGA);
  ~FPGA();

private:
  fpga::Bus bus;
  fpga::InterconnectProtocol interconnectprotocol;
  fpga::RAM ram;

  void initialize();
};