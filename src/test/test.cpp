#include <iostream>

#include "system.h"

int main() {
  SystemLoader loader("system.yaml", "./configs");
  const auto &sys = loader.get_system();

  for (const auto &[name, chiplet] : sys.chiplets) {
    std::cout << "Chiplet: " << name << " type=" << chiplet.type.str()
              << " clk_cycle=" << chiplet.config["cores"]["clk_cycle"].as<int>()
              << "\n";
  }

  std::cout << "Interconnect: " << sys.interconnect.type.str() << "\n";

  for (const auto &conn : sys.interconnect.connections) {
    std::cout << "  " << conn.from << " -> " << conn.to
              << " dist=" << conn.distance_um << "um\n";
  }
}