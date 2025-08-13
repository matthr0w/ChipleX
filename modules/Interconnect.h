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

SC_MODULE(Interconnect) {
public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;
  BufferUsageTracker tx_tracker;
  BufferUsageTracker rx_tracker;
  unsigned int incoming_flits;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<Interconnect> protocol_target_socket;
  simple_initiator_socket<Interconnect> protocol_initiator_socket;
  simple_target_socket<Interconnect> interconnect_target_socket;
  simple_initiator_socket<Interconnect> interconnect_initiator_socket;

  Interconnect(sc_module_name name, unsigned int buffer_size,
               unsigned int flit_size, unsigned int overhead_size,
               sc_time pre_delay, sc_time post_delay, sc_time irq_delay,
               double bandwidth, double distance);

private:
  void process_tx_buffer();
  void process_rx_buffer();

  std::deque<tlm_generic_payload *> tx_buffer;
  std::deque<tlm_generic_payload *> rx_buffer;
  unsigned int tx_buffer_used_bytes;
  unsigned int rx_buffer_used_bytes;

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int buffer_size;
  const unsigned int flit_size;
  const unsigned int overhead_size;
  const sc_time pre_delay;
  const sc_time post_delay;
  const sc_time irq_delay;
  double bandwidth;
  double distance;

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm_generic_payload> peq_protocol;
  void process_protocol_transaction();
  peq_with_get<tlm_generic_payload> peq_interconnect;
  void process_interconnect_transaction();

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event protocol_transaction_done;
  sc_event interconnect_transaction_done;
  sc_event tx_buffer_in_event;
  sc_event tx_buffer_out_event;
  sc_event rx_buffer_in_event;
  sc_event rx_buffer_out_event;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_protocol(tlm_generic_payload & transaction,
                                         tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw_protocol(tlm_generic_payload & transaction,
                                         tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_fw_interconnect(
      tlm_generic_payload & transaction, tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw_interconnect(
      tlm_generic_payload & transaction, tlm_phase & phase, sc_time & delay);
};