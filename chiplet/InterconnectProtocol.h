#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/protocol/ChipletExtension.h"

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

  const unsigned int flit_size =
      interconnect_config.get<unsigned int>("interconnect_protocol.flit_size");
  const unsigned int header_size = interconnect_config.get<unsigned int>(
      "interconnect_protocol.header_size");
  const sc_time pre_delay =
      interconnect_config.get<sc_time>("interconnect_protocol.pre_delay");
  const sc_time post_delay =
      interconnect_config.get<sc_time>("interconnect_protocol.post_delay");
  const sc_time irq_delay =
      interconnect_config.get<sc_time>("interconnect_protocol.irq_delay");

  const unsigned int chiplet_id;
  uint32_t write_address;

  std::map<tlm::tlm_generic_payload*, int> transaction_id_map;

  void send_to_phy(tlm_generic_payload & transaction);
  void send_to_bus(tlm_generic_payload & transaction);
  void send_irq(tlm_generic_payload & transaction, tlm_command command);

  void send_flits(tlm_generic_payload & transaction);
  void set_write_address(tlm_generic_payload & transaction);

  //
  bool is_request(const ChipletExtension *ext);
  unsigned int get_required_flit_count(tlm_generic_payload & transaction);
  unsigned int get_available_data_bytes_per_flit(tlm_generic_payload &
                                                 transaction);

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm_generic_payload> peq_bus;
  void process_bus_transaction();
  peq_with_get<tlm_generic_payload> peq_phy;
  void process_phy_transaction();

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
}; // namespace chiplet