#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <vector>

#include "common/Tracker.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

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

  Cache(sc_module_name name, unsigned int chip_id, unsigned int cache_size,
        unsigned int cache_block_size, sc_time cache_arbitration_delay,
        sc_time cache_access_delay, unsigned int bus_width,
        sc_time bus_clk_cycle);

  void report_rates();

private:
  void split_transaction(tlm_generic_payload & transaction);
  void send_to_bus(tlm_generic_payload & transaction);

  struct CacheLine {
    bool valid = false;
    uint32_t tag = 0;
    std::vector<uint8_t> data;

    CacheLine() : data(32, 0) {}
  };

  std::vector<CacheLine> cache_lines;
  unsigned int num_lines;
  unsigned int num_accesses;
  unsigned int num_hits;
  unsigned int num_misses;

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int chip_id;
  const unsigned int cache_size;
  const unsigned int cache_block_size;
  const sc_time cache_arbitration_delay;
  const sc_time cache_access_delay;
  const unsigned int bus_width;
  const sc_time bus_clk_cycle;

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