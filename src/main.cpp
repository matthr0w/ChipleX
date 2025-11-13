#include <vector>

#include "globals.h"

#include "common/Parser.h"
#include "common/Router.h"
#include "common/Statistics.h"
#include "modules/chiplets/ChipletBase.h"
#include "modules/chiplets/CoreChiplet.h"
#include "modules/chiplets/MemoryChiplet.h"
#include "setup/Loader.h"

int sc_main(int argc, char *argv[]) {
  // Parse command line arguments
  Parser parser(argc, argv);
  // Start statistics manager
  StatManager &stats = StatManager::instance();
  stats.start_simulation_timer();
  // Load system setup
  SetupLoader setup("./configs", "./setups", sim_setup);
  SystemConfig sysconf = setup.get_config();
  // Initialize router instance
  Router::instance().init(sysconf);

  // Create chiplets
  std::map<std::string, std::unique_ptr<ChipletBase>> chiplets;
  for (const auto &[chiplet_name, chiplet] : sysconf.chiplets) {
    auto chiplet_id = sysconf.chiplet_ids.find(chiplet_name)->second;
    switch (sysconf.chiplets[chiplet_name].type.value) {
    case ChipletType::Type::SingleCore:
    case ChipletType::Type::DualCore:
    case ChipletType::Type::QuadCore:
      chiplets[chiplet_name] = std::make_unique<CoreChiplet>(
          chiplet_name.c_str(), chiplet_id, sysconf);
      break;
    case ChipletType::Type::Memory:
      chiplets[chiplet_name] = std::make_unique<MemoryChiplet>(
          chiplet_name.c_str(), chiplet_id, sysconf);
      break;
    default:
      break;
    }
  }

  // Connect chiplets
  for (int i = 0; i < sysconf.connections.size(); ++i) {
    const auto &conn = sysconf.connections[i];
    chiplets[conn.endpoint0.chiplet_name]
        ->interconnects[conn.endpoint0.interconnect_name]
        ->link_out_ports[conn.endpoint0.link_id]
        ->bind(*chiplets.find(conn.endpoint1.chiplet_name)
                    ->second->interconnects[conn.endpoint1.interconnect_name]
                    ->link_in_ports[conn.endpoint1.link_id]);
    chiplets[conn.endpoint1.chiplet_name]
        ->interconnects[conn.endpoint1.interconnect_name]
        ->link_out_ports[conn.endpoint1.link_id]
        ->bind(*chiplets.find(conn.endpoint0.chiplet_name)
                    ->second->interconnects[conn.endpoint0.interconnect_name]
                    ->link_in_ports[conn.endpoint0.link_id]);
  }

  if (sim_duration == SC_ZERO_TIME)
    sc_start();
  else
    sc_start(sim_duration);

  stats.end_simulation_timer();
  stats.dump_to_file("stats.json");

  return 0;
}