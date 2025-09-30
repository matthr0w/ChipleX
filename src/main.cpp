#include <iostream>
#include <vector>

#include "globals.h"

#include "common/Parser.h"
#include "common/Router.h"
#include "common/System.h"

#include "modules/Chiplet.h"

#include "usercode/UserCode.h"

int sc_main(int argc, char *argv[]) {
  Parser parser(argc, argv);

  // Initialize system loader
  SystemLoader sysloader("system.yaml", "./configs");
  SystemConfig sysconf = sysloader.get_config();
  // Initialize router instance
  Router::instance().init(sysconf);

  auto start_timestamp = std::chrono::high_resolution_clock::now();

  // Create chiplets
  std::vector<Chiplet *> chiplets;
  unsigned num_chiplets = sysconf.chiplets.size();
  chiplets.reserve(num_chiplets);

  for (int i = 0; i < num_chiplets; ++i) {
    chiplets.push_back(new Chiplet(sysconf.chiplet_order[i].c_str(), sysconf));
    // Assign user code
    Chiplet *chiplet = chiplets[i];
    for (int c = 0; c < chiplet->num_cores; ++c) {
      auto it = core_code.find({i, c});
      if (it != core_code.end()) {
        chiplet->cores[c]->thread_fn = it->second.first;
        chiplet->cores[c]->interrupt_fn = it->second.second;
      }
    }
  }

  // Connect chiplets
  for (int i = 0; i < sysconf.interconnect.connections.size(); ++i) {
    const auto &conn = sysconf.interconnect.connections[i];
    chiplets[conn.endpoint0.chiplet_id]
        ->interconnect->link_out_ports[conn.endpoint0.link_id]
        ->bind(*chiplets[conn.endpoint1.chiplet_id]
                    ->interconnect->link_in_ports[conn.endpoint1.link_id]);
    chiplets[conn.endpoint1.chiplet_id]
        ->interconnect->link_out_ports[conn.endpoint1.link_id]
        ->bind(*chiplets[conn.endpoint0.chiplet_id]
                    ->interconnect->link_in_ports[conn.endpoint0.link_id]);
  }

  std::cout << std::endl;

  if (sim_duration == SC_ZERO_TIME) {
    sc_start();
  } else {
    sc_start(sim_duration);
  }

  auto stop_timestamp = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      stop_timestamp - start_timestamp);

  // -------------------------------------------------------
  // Statistics
  // -------------------------------------------------------
  std::cout << "=== Statistics ===" << std::endl;
  std::cout << "Simulation Time: " << sc_time_stamp() << std::endl;
  std::cout << "Execution Time: " << std::dec << duration.count() << " ms\n";

  for (auto *chiplet : chiplets)
    delete chiplet;
  chiplets.clear();

  return 0;
}