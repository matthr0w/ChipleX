#pragma once

#include <systemc>

#include "Bus.h"
#include "Core.h"
#include "Interconnect.h"
#include "RAM.h"

SC_MODULE(Chiplet) {
private:
  static unsigned int instance;
  const unsigned int chiplet_id;

public:
  Interconnect interconnect;

  SC_CTOR(Chiplet);

private:
  Bus bus;
  Core core0;
  Core core1;
  RAM ram;

  void initialize();
};