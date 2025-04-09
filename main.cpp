#include <systemc>

#include "chiplet/Chiplet.h"

using namespace sc_core;
using namespace tlm;

int sc_main(int argc, char *argv[]) {
  sc_clock clk("clk", 10, SC_NS);

  Chiplet chiplet("Chiplet");
  chiplet.clk(clk);

  sc_start(1000, SC_NS);

  return 0;
}