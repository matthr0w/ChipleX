#pragma once

#include <systemc>

#include "Bus.h"
#include "Core.h"
#include "RAM.h"

SC_MODULE(Chiplet) {
private:
  static unsigned int instance;
  const unsigned int id;

public:
  static unsigned int total_instances;

  // public for interconnect
  Bus bus;

  SC_CTOR(Chiplet);

private:
  Core core1;
  Core core2;
  RAM ram;

  void initialize();
};