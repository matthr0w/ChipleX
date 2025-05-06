#pragma once

#include <systemc>

#include "Bus.h"
#include "Core.h"
#include "Interconnect.h"
#include "InterconnectProtocol.h"
#include "RAM.h"

#include "include/globals.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Chiplet) {
private:
  static unsigned int instance;
  const unsigned int chiplet_id;

public:
  std::vector<chiplet::Interconnect *> interconnects;

  SC_CTOR(Chiplet);
  ~Chiplet();

private:
  chiplet::Bus bus;
  chiplet::Core core0;
  chiplet::Core core1;
  chiplet::InterconnectProtocol interconnectprotocol;
  chiplet::RAM ram;

  void initialize();
};