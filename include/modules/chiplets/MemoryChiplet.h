#include "modules/Memory.h"
#include "modules/chiplets/ChipletBase.h"
#include "modules/interconnects/InterconnectBase.h"
#include "modules/interconnects/Manager.h"

struct MemoryChiplet : ChipletBase {
  Memory memory;
  
  // Dummy AXI initiator socket
  ARM::AXI::SimpleInitiatorSocket<MemoryChiplet> dummy_axi_isocket;

  MemoryChiplet(sc_core::sc_module_name name, unsigned id, SystemConfig sysconf)
      : ChipletBase(name, id, sysconf),
        memory("memory", sysconf.chiplets[chiplet_name].config),
        dummy_axi_isocket("dummy_axi_isocket", *this, nullptr,
                          ARM::TLM::PROTOCOL_AXI4,
                          sysconf.chiplets[chiplet_name]
                              .config["axi"]["width"]
                              .as<unsigned>()) {
    // Memory
    memory.clk.bind(system_clk);

    // Interconnect
    InterconnectManager manager(chiplet_id, sysconf.chiplets[chiplet_name],
                                sysconf.interconnect, nullptr);
    interconnect = manager.create_interconnect();
    interconnect->bind_clock(system_clk);
    interconnect->axi_in_port->bind(dummy_axi_isocket);
    interconnect->axi_out_port->bind(memory.tsocket);
  }
};