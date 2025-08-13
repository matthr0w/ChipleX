#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/Tracker.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(InterconnectProtocol) {
public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<InterconnectProtocol> bus_target_socket;
  simple_initiator_socket<InterconnectProtocol> bus_initiator_socket;

  simple_target_socket_tagged<InterconnectProtocol>
      *interconnect_target_sockets;
  simple_initiator_socket_tagged<InterconnectProtocol>
      *interconnect_initiator_sockets;

  simple_initiator_socket_tagged<InterconnectProtocol> *irq_initiator_sockets;

  InterconnectProtocol(sc_module_name name, unsigned int chip_id,
                       unsigned int num_cores, unsigned int num_interconnects,
                       unsigned int flit_size, unsigned int overhead_size,
                       sc_time pre_delay, sc_time post_delay, sc_time irq_delay,
                       unsigned int bus_width, sc_time bus_clk_cycle);
  ~InterconnectProtocol();

private:
  int current_interconnect;

  struct InterconnectRequest {
    int interconnect_id;
    tlm_generic_payload *transaction;
  };

  std::deque<InterconnectRequest> request_queue;

  void process_bus_transaction();
  void process_phy_transaction();
  void process_queue();

  void send_flits(tlm_generic_payload & transaction);
  void send_to_phy(tlm_generic_payload & transaction);
  void send_to_bus(tlm_generic_payload & transaction);
  void send_irq(tlm_generic_payload & transaction, tlm_command command);

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int chip_id;
  const unsigned int flit_size;
  const unsigned int overhead_size;
  const sc_time pre_delay;
  const sc_time post_delay;
  const sc_time irq_delay;
  const unsigned int bus_width;
  const sc_time bus_clk_cycle;

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm_generic_payload> peq_bus;
  peq_with_get<tlm_generic_payload> peq_phy;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event bus_transaction_done;
  sc_event phy_transaction_done;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_bus(tlm_generic_payload & transaction,
                                    tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw_bus(tlm_generic_payload & transaction,
                                    tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_fw_interconnect(int id,
                                             tlm_generic_payload &transaction,
                                             tlm_phase &phase, sc_time &delay);

  tlm_sync_enum nb_transport_bw_interconnect(int id,
                                             tlm_generic_payload &transaction,
                                             tlm_phase &phase, sc_time &delay);
};