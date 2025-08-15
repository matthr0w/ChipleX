#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <vector>

#include "common/Tracker.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(RAM) {
public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<RAM> socket;

  RAM(sc_module_name name, unsigned int ram_size, unsigned int ram_width,
      sc_time ram_clk_cycle, sc_time ram_address_delay,
      sc_time ram_access_delay);

  void report_usage();

private:
  std::vector<uint8_t> mem;
  std::vector<bool> write_flags;

  struct Request {
    tlm_generic_payload *transaction;
    tlm_phase *phase;
    sc_time *delay;
  };

  std::deque<Request> requests_queue;
  void process_queue();

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int ram_size;
  const unsigned int ram_width;
  const sc_time ram_clk_cycle;
  const sc_time ram_address_delay;
  const sc_time ram_access_delay;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event request_issued;
  sc_event transaction_done;

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
};