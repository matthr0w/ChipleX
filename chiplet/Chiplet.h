#pragma once

#include <systemc>

#include "Bus.h"
#include "Core.h"
#include "RAM.h"

SC_MODULE(Chiplet) {
public:
  // public for interconnect
  Bus bus;

  SC_CTOR(Chiplet);

private:
  Core core1;
  Core core2;
  RAM ram;

  void initialize();
};