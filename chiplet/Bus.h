#pragma once

#include <systemc>

#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

const std::array<std::string, 6> modules = {
    "Unassigned", "Core0", "Core1", "Interconnect0", "RAM"};

struct BusRequest {
  int module;
  tlm::tlm_generic_payload *transaction;
};

SC_MODULE(Bus) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  tlm_utils::simple_target_socket_tagged<Bus> core0_target_socket;
  tlm_utils::simple_target_socket_tagged<Bus> core1_target_socket;
  tlm_utils::simple_target_socket_tagged<Bus> interconnect_target_socket;
  tlm_utils::simple_initiator_socket<Bus> interconnect_initiator_socket;
  tlm_utils::simple_initiator_socket_tagged<Bus> ram_initiator_socket;

  Bus(sc_core::sc_module_name name, unsigned int chiplet_id);

private:
  const unsigned int chiplet_id;
  unsigned int current_owner;

  std::deque<BusRequest> request_queue;

  void process_transaction_fw();
  void process_transaction_bw();
  void process_queue();

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  tlm_utils::peq_with_get<tlm::tlm_generic_payload> peq_fw;
  tlm_utils::peq_with_get<tlm::tlm_generic_payload> peq_bw;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm::tlm_sync_enum nb_transport_fw(
      int id, tlm::tlm_generic_payload &transaction, tlm::tlm_phase &phase,
      sc_core::sc_time &delay);
  tlm::tlm_sync_enum nb_transport_bw(
      int id, tlm::tlm_generic_payload &transaction, tlm::tlm_phase &phase,
      sc_core::sc_time &delay);
};