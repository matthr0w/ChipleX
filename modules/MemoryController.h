#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/Tracker.h"

#include "include/logging.h"

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
  simple_target_socket<MemoryController> tsocket;
  simple_initiator_socket<MemoryController> isocket;

  MemoryController(sc_module_name name, unsigned int ram_size,
                   sc_time address_assignment_delay);

private:
  sc_mutex mutex;

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

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int ram_size;
  const sc_time address_assignment_delay;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);

  // -------------------------------------------------------
  // delay model
  // -------------------------------------------------------
  struct DelayModel {
  private:
    const MemoryController &module;

  public:
    DelayModel(const MemoryController &m) : module(m) {}

    sc_time address_assignment(tlm_generic_payload &transaction) const {
      SC_LOG_DELAY(&module, transaction, "Address Assignment",
                   module.address_assignment_delay);
      return module.address_assignment_delay;
    }
  };

  DelayModel delays{*this};
};