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
  // Initialize setup loader
  SetupLoader setup("./configs", "./setups", sim_setup);
  SystemConfig sysconf = setup.get_config();
  // Initialize router instance
  Router::instance().init(sysconf);

  // Create chiplets
  std::vector<ChipletBase *> chiplets;
  unsigned num_chiplets = sysconf.chiplets.size();
  chiplets.reserve(num_chiplets);

  for (int i = 0; i < num_chiplets; ++i) {
    std::string chiplet_name = sysconf.chiplet_order[i];
    switch (sysconf.chiplets[chiplet_name].type.type) {
    case ChipletType::Type::SingleCore:
    case ChipletType::Type::DualCore:
    case ChipletType::Type::QuadCore:
      chiplets.push_back(
          new CoreChiplet(sysconf.chiplet_order[i].c_str(), i, sysconf));
      break;
    case ChipletType::Type::Memory:
      chiplets.push_back(
          new MemoryChiplet(sysconf.chiplet_order[i].c_str(), i, sysconf));
      break;
    default:
      break;
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

  if (sim_duration == SC_ZERO_TIME)
    sc_start();
  else
    sc_start(sim_duration);

  for (auto *chiplet : chiplets)
    delete chiplet;
  chiplets.clear();

  stats.end_simulation_timer();
  stats.dump_to_file("stats.json");

  return 0;
}