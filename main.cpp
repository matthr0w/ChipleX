#include <systemc>

#include "chiplet/Chiplet.h"

using namespace sc_core;
using namespace tlm;

int sc_main(int argc, char *argv[]) {
  Chiplet chiplet("Chiplet");

  sc_start(1000, SC_NS);

  return 0;
}