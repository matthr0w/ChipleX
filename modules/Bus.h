#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/Tracker.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Bus) {
public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket_tagged<Bus> *manager_target_sockets;
  simple_initiator_socket_tagged<Bus> *subordinate_initiator_sockets;

  Bus(sc_module_name name, unsigned int chip_id, unsigned int num_managers,
      unsigned int num_subordinates, sc_time bus_arbitration_delay);
  ~Bus();

private:
  int current_owner;

  struct BusRequest {
    int module;
    tlm_generic_payload *transaction;
  };

  std::deque<BusRequest> request_queue;

  void process_transaction_fw();
  void process_transaction_bw();
  void process_queue();

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int chip_id;
  const sc_time bus_arbitration_delay;

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm_generic_payload> peq_fw;
  peq_with_get<tlm_generic_payload> peq_bw;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(int id, tlm_generic_payload &transaction,
                                tlm_phase &phase, sc_time &delay);
  tlm_sync_enum nb_transport_bw(int id, tlm_generic_payload &transaction,
                                tlm_phase &phase, sc_time &delay);
};