#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <vector>

#include "common/Tracker.h"

#include "include/logging.h"

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
  simple_target_socket<RAM> tsocket;

  RAM(sc_module_name name, unsigned int ram_size, unsigned int ram_width,
      sc_time ram_clk_cycle, sc_time ram_access_delay);

  void report_usage();

private:
  struct Request {
    tlm_generic_payload *transaction;
    tlm_phase *phase;
    sc_time *delay;
  };

  std::deque<Request> request_queue;
  void process_request_queue();

  std::vector<uint8_t> mem;
  std::vector<bool> write_flags;

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int ram_size;
  const unsigned int ram_width;
  const sc_time ram_clk_cycle;
  const sc_time ram_access_delay;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event req_evt;
  sc_event resp_evt;

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

  // -------------------------------------------------------
  // delay model
  // -------------------------------------------------------
  struct DelayModel {
  private:
    const RAM &module;

  public:
    DelayModel(const RAM &m) : module(m) {}

    sc_time ram_access(tlm_generic_payload &transaction) const {
      unsigned int data_size = transaction.get_data_length();
      unsigned int num_cycles =
          (data_size * 8 + module.ram_width - 1) / module.ram_width;

      sc_time delay =
          module.ram_access_delay + module.ram_clk_cycle * num_cycles;

      SC_LOG_DELAY(&module, transaction, "RAM Access", delay);
      return delay;
    }
  };

  DelayModel delays{*this};
};