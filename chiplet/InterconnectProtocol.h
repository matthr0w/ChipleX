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
SC_MODULE(InterconnectProtocol) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket_tagged<InterconnectProtocol>
      *interconnect_target_sockets;
  simple_initiator_socket_tagged<InterconnectProtocol>
      *interconnect_initiator_sockets;

  simple_target_socket<InterconnectProtocol> bus_target_socket;
  simple_initiator_socket<InterconnectProtocol> bus_initiator_socket;
  simple_initiator_socket<InterconnectProtocol> core0_irq_initiator_socket;
  simple_initiator_socket<InterconnectProtocol> core1_irq_initiator_socket;

  InterconnectProtocol(sc_core::sc_module_name name, unsigned int chiplet_id);
  ~InterconnectProtocol();

private:
  const Config &chiplet_config = ConfigRegistry::instance().get("Chiplet");
  const Config &interconnect_config =
      ConfigRegistry::instance().get("Interconnect");

  const unsigned int chiplet_id;
  uint32_t write_address;

  void process_tx_buffer();
  void process_rx_buffer();

  void process_bus_transaction(tlm_generic_payload & transaction);
  void send_to_interconnect(tlm_generic_payload & transaction);
  void send_irq(tlm_generic_payload & transaction, tlm_command command);

  void set_write_address(tlm_generic_payload & transaction);

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
}; // namespace chiplet