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
  simple_target_socket<Cache> target_socket;
  simple_initiator_socket<Cache> initiator_socket;

  Cache(sc_module_name name, unsigned int chip_id, unsigned int cache_size,
        unsigned int cache_block_size, sc_time cache_arbitration_delay,
        sc_time cache_access_delay, unsigned int bus_width,
        sc_time bus_clk_cycle);

  void report_rates();

private:
  struct Request {
    tlm_generic_payload *transaction;
    tlm_phase *phase;
    sc_time *delay;
  };

  std::deque<Request> requests_queue;
  void process_queue();

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

  void access_cache(tlm_generic_payload & transaction);

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
  // events
  // -------------------------------------------------------
  sc_event req_evt;
  sc_event resp_evt;
  sc_event axi_resp_evt;

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm_generic_payload> peq;
  void process_transaction();

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);

  void transport_fw(tlm_generic_payload & transaction);
};