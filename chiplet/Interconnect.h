#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "include/configs.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

namespace chiplet {
SC_MODULE(Interconnect) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<Interconnect> protocol_target_socket;
  simple_initiator_socket<Interconnect> protocol_initiator_socket;
  simple_target_socket<Interconnect> interconnect_target_socket;
  simple_initiator_socket<Interconnect> interconnect_initiator_socket;

  Interconnect(sc_module_name name, double bandwidth, double distance);

private:
  const Config &interconnect_config =
      ConfigRegistry::instance().get("Interconnect");

  double bandwidth;
  double distance;

  void process_tx_buffer();
  void process_rx_buffer();

  std::deque<tlm_generic_payload *> tx_buffer;
  std::deque<tlm_generic_payload *> rx_buffer;
  unsigned tx_buffer_used_bytes;
  unsigned rx_buffer_used_bytes;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event tx_transaction_done;
  sc_event rx_transaction_done;
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
}; // namespace chiplet