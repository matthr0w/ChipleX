#include "globals.h"

#include "common/Parser.h"
#include "common/Router.h"
#include "common/Statistics.h"
#include "modules/chiplets/ChipletBase.h"
#include "modules/chiplets/ComputeChiplet.h"
#include "modules/chiplets/MemoryChiplet.h"
#include "setup/Loader.h"

int sc_main(int argc, char *argv[]) {
	// Parse command line arguments
	Parser parser(argc, argv);
	// Start statistics manager
	StatManager &stats = StatManager::instance();
	stats.start_simulation_timer();
	// Load system setup
	SetupLoader  setup("./configs", "./setups", sim_setup);
	SystemConfig sysconf = setup.get_config();
	// Initialize router instance
	Router::instance().init(sysconf);

	// Create chiplets
	std::map<std::string, std::unique_ptr<ChipletBase>> chiplets;
	for (const auto &[chiplet_name, chiplet_config] : sysconf.chiplets) {
		auto chiplet_id = sysconf.chiplet_ids.find(chiplet_name)->second;
		switch (sysconf.chiplets[chiplet_name].type.value) {
		case ChipletType::Type::Compute:
			chiplets[chiplet_name] =
			    std::make_unique<ComputeChiplet>(chiplet_name.c_str(), chiplet_id, chiplet_config, sysconf.cycles);
			break;
		case ChipletType::Type::Memory:
			chiplets[chiplet_name] = std::make_unique<MemoryChiplet>(chiplet_name.c_str(), chiplet_id, chiplet_config);
			break;
		default:
			break;
		}
	}

	// Connect chiplets
	for (size_t i = 0; i < sysconf.connections.size(); ++i) {
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

	// A fatal error or failed assertion in any process throws; catch it here so
	// the simulation shuts down cleanly (with partial stats) instead of calling
	// std::terminate.
	int exit_code = 0;
	try {
		if (sim_duration == SC_ZERO_TIME) {
			sc_start();
		} else {
			sc_start(sim_duration);
		}
	} catch (const std::exception &e) {
		std::cerr << "Simulation aborted at " << sc_time_stamp() << ": " << e.what() << std::endl;
		exit_code = 1;
	}

	stats.end_simulation_timer();
	stats.dump_to_file("stats.json");

	return exit_code;
}