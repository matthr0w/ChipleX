#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <vector>

#include "common/Tracker.h"

#include "include/logging.h"

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
  simple_target_socket<Cache> tsocket;
  simple_initiator_socket<Cache> isocket;

  Cache(sc_module_name name, unsigned int chip_id, unsigned int cache_size,
        unsigned int cache_block_size, unsigned int cache_store_buffer_size,
        sc_time cache_arbitration_delay, sc_time cache_access_delay);

  void dump();
  void report_rates();

private:
  void send_axi_request(tlm_generic_payload & transaction, tlm_command command,
                        uint32_t address, unsigned char *data,
                        unsigned int data_length);

  struct Request {
    tlm_generic_payload *transaction;
    tlm_phase *phase;
    sc_time *delay;
  };

  std::deque<Request> request_queue;
  void process_request_queue();

  struct StoreBufferEntry {
    tlm_generic_payload *transaction;
    uint32_t address;
    std::vector<uint8_t> data;
    bool wlast;
  };

  std::deque<StoreBufferEntry> store_buffer;
  void drain_store_buffer();

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

  std::unordered_map<tlm::tlm_generic_payload *, bool> cache_skipped;

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int chip_id;
  const unsigned int cache_size;
  const unsigned int cache_block_size;
  const unsigned int cache_store_buffer_size;
  const sc_time cache_arbitration_delay;
  const sc_time cache_access_delay;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event req_evt;
  sc_event resp_evt;
  sc_event axi_resp_evt;
  sc_event store_buffer_in_evt;
  sc_event store_buffer_out_evt;

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

  // -------------------------------------------------------
  // delay model
  // -------------------------------------------------------
  struct DelayModel {
  private:
    const Cache &module;

  public:
    DelayModel(const Cache &m) : module(m) {}

    sc_time cache_arbitration(tlm_generic_payload &transaction) const {
      SC_LOG_DELAY(&module, transaction, "Cache Arbitration",
                   module.cache_arbitration_delay);
      return module.cache_arbitration_delay;
    }

    sc_time cache_access(tlm_generic_payload &transaction) const {
      SC_LOG_DELAY(&module, transaction, "Cache Access",
                   module.cache_access_delay);
      return module.cache_access_delay;
    }
  };

  DelayModel delays{*this};
};