#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "include/configs.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

namespace chiplet {
SC_MODULE(MemoryController) {
public:
  simple_target_socket<MemoryController> bus_target_socket;
  simple_initiator_socket<MemoryController> ram_initiator_socket;

  SC_CTOR(MemoryController);

private:
  uint32_t offchip_base_address;
  std::map<uint32_t, unsigned int> allocated_ranges;
  std::unordered_map<int, uint32_t> pending_flit_writes;

  void set_address(tlm_generic_payload & transaction);

  uint32_t allocate_dynamic_address(tlm_generic_payload & transaction,
                                    bool onchip, unsigned int size);
  void deallocate_dynamic_address(tlm_generic_payload & transaction,
                                  uint32_t address, unsigned int size);

  void send_to_ram(tlm_generic_payload & transaction);

  // -------------------------------------------------------
  // config
  // -------------------------------------------------------
  const Config &chiplet_config = ConfigRegistry::instance().get("Chiplet");

  const unsigned int bus_width = chiplet_config.get<unsigned int>("bus.width");
  const sc_time bus_clk_cycle = chiplet_config.get<sc_time>("bus.clk_cycle");
  const unsigned int ram_size = chiplet_config.get<unsigned int>("ram.size");

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm_generic_payload> peq;
  void process_transaction();

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event ram_transaction_done;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);
};
}; // namespace chiplet