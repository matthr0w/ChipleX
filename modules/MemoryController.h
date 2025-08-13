#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/Tracker.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(MemoryController) {
public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<MemoryController> bus_target_socket;
  simple_initiator_socket<MemoryController> ram_initiator_socket;

  MemoryController(sc_module_name name, unsigned int bus_width,
                   sc_time bus_clk_cycle, unsigned int ram_size);

private:
  uint32_t offchip_base_address;
  std::map<uint32_t, unsigned int> allocated_ranges;

  struct FlitKey {
    int request_id;
    int source_id;
    int core_id;

    bool operator==(const FlitKey &other) const {
      return request_id == other.request_id && source_id == other.source_id &&
             core_id == other.core_id;
    }
  };

  struct FlitKeyHash {
    std::size_t operator()(const FlitKey &key) const {
      std::size_t h1 = std::hash<int>()(key.request_id);
      std::size_t h2 = std::hash<int>()(key.source_id);
      std::size_t h3 = std::hash<int>()(key.core_id);
      return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
  };

  std::unordered_map<FlitKey, uint32_t, FlitKeyHash> pending_flit_writes;

  void set_address(tlm_generic_payload & transaction);
  void set_flit_address(tlm_generic_payload & transaction);

  uint32_t allocate_dynamic_address(tlm_generic_payload & transaction,
                                    bool onchip, unsigned int size);
  void deallocate_dynamic_address(tlm_generic_payload & transaction,
                                  uint32_t address, unsigned int size);

  void send_to_ram(tlm_generic_payload & transaction);

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int bus_width;
  const sc_time bus_clk_cycle;
  const unsigned int ram_size;

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