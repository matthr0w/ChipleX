#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <vector>

#include "common/Tracker.h"

#include "include/configs.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

namespace chiplet {
SC_MODULE(Cache) {
public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<Cache> core_target_socket;
  simple_initiator_socket<Cache> bus_initiator_socket;

  Cache(sc_module_name name, unsigned int chiplet_id);

private:
  const unsigned int chiplet_id;

  void split_transaction(tlm_generic_payload & transaction);
  void parse_address(uint32_t address, uint32_t &tag, uint32_t &index);
  void send_to_bus(tlm_generic_payload & transaction);

  struct CacheLine {
    bool valid = false;
    uint32_t tag = 0;
    std::vector<uint8_t> data;

    CacheLine() : data(32, 0) {}
  };

  std::vector<CacheLine> cache_lines;
  unsigned int num_lines;

  // -------------------------------------------------------
  // config
  // -------------------------------------------------------
  const Config &chiplet_config = ConfigRegistry::instance().get("Chiplet");

  const unsigned int cache_size =
      chiplet_config.get<unsigned int>("cache.size");
  const unsigned int block_size =
      chiplet_config.get<unsigned int>("cache.block_size");
  const sc_time arbitration_delay =
      chiplet_config.get<sc_time>("cache.arbitration_delay");
  const sc_time access_delay =
      chiplet_config.get<sc_time>("cache.access_delay");
  const unsigned int bus_width = chiplet_config.get<unsigned int>("bus.width");
  const sc_time bus_clk_cycle = chiplet_config.get<sc_time>("bus.clk_cycle");

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm_generic_payload> peq;
  void process_transaction();

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event bus_transaction_done;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);
};
}; // namespace chiplet