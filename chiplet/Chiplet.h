#pragma once

#include <systemc>

#include "Bus.h"
#include "Core.h"
#include "RAM.h"

SC_MODULE(Chiplet) {
public:
  sc_core::sc_in<bool> clk;

  SC_CTOR(Chiplet);

private:
  Core core1;
  Core core2;
  RAM ram;
  Bus bus;

  void initialize();
};