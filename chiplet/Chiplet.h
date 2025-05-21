#pragma once

#include <systemc>

#include "Bus.h"
#include "Core.h"
#include "Interconnect.h"
#include "InterconnectProtocol.h"
#include "RAM.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Chiplet) {
public:
  chiplet::Core core0;
  chiplet::Core core1;
  std::vector<chiplet::Interconnect *> interconnects;

  SC_CTOR(Chiplet);
  ~Chiplet();

private:
  const Config &interconnect_config =
      ConfigRegistry::instance().get("Interconnect");

  static unsigned int instance;
  const unsigned int chiplet_id;

  chiplet::Bus bus;
  chiplet::InterconnectProtocol interconnectprotocol;
  chiplet::RAM ram;

  void initialize();
};