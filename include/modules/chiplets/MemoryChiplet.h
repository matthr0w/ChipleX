#include "modules/Memory.h"
#include "modules/chiplets/ChipletBase.h"
#include "modules/interconnects/InterconnectBase.h"
#include "modules/interconnects/Manager.h"

struct MemoryChiplet : ChipletBase {
  const YAML::Node chiplet_config;

  Memory memory;

  // Dummy AXI initiator socket
  ARM::AXI::SimpleInitiatorSocket<MemoryChiplet> dummy_axi_isocket;

  MemoryChiplet(sc_module_name name, unsigned id, SystemConfig sysconf)
      : ChipletBase(name, id, sysconf),
        chiplet_config(sysconf.chiplets[chiplet_name].config),
        memory("memory", sysconf.chiplets[chiplet_name].config),
        dummy_axi_isocket("dummy_axi_isocket", *this, nullptr,
                          ARM::TLM::PROTOCOL_AXI4,
                          chiplet_config["axi"]["width"].as<int>()) {
    // Assertions
    LOG_ASSERT(chiplet_config["axi"]["width"].as<int>() >= 8 ||
                   (chiplet_config["axi"]["width"].as<int>() % 8) == 0,
               "Parameter Error: AXI size must be a multiple of 8");

    // Memory
    memory.clk.bind(system_clk);

    // Interconnect
    InterconnectManager manager(chiplet_id, sysconf.chiplets[chiplet_name],
                                sysconf.interconnect, 0, nullptr);
    interconnect = manager.create_interconnect();
    interconnect->bind_clock(system_clk);
    interconnect->axi_in_port->bind(dummy_axi_isocket);
    interconnect->axi_out_port->bind(memory.tsocket);
  }
};