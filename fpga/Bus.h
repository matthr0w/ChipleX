#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

namespace fpga {
SC_MODULE(Bus) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket_tagged<Bus> generator_target_socket;
  simple_target_socket_tagged<Bus> interconnect_target_socket;
  simple_initiator_socket_tagged<Bus> interconnect_initiator_socket;
  simple_initiator_socket_tagged<Bus> ram_initiator_socket;

  Bus(sc_module_name name, unsigned int fpga_id);

private:
  const unsigned int fpga_id;
  unsigned int current_owner;

  const std::array<std::string, 4> modules = {"Unassigned", "Generator",
                                              "Interconnect", "RAM"};

  struct BusRequest {
    int module;
    tlm_generic_payload *transaction;
  };

  std::deque<BusRequest> request_queue;

  void process_transaction_fw();
  void process_transaction_bw();
  void process_queue();

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
}; // namespace fpga