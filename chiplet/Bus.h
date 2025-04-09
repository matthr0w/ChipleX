#pragma once

#include <systemc>

#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <deque>

struct BusRequest {
  int core_id;
  tlm::tlm_generic_payload *payload;
};

SC_MODULE(Bus) {
public:
  tlm_utils::simple_target_socket_tagged<Bus> target_socket_core1;
  tlm_utils::simple_target_socket_tagged<Bus> target_socket_core2;
  tlm_utils::simple_initiator_socket<Bus> initiator_socket;

  SC_CTOR(Bus);

private:
  std::deque<BusRequest> m_request_queue;

  sc_core::sc_time arbitration_delay;


  tlm_utils::peq_with_get<tlm::tlm_generic_payload> peq;


  int current_owner;


  tlm::tlm_sync_enum nb_transport_fw(int id, tlm::tlm_generic_payload &payload,
                                     tlm::tlm_phase &phase,
                                     sc_core::sc_time &delay);
  tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload & payload,
                                     tlm::tlm_phase & phase,
                                     sc_core::sc_time & delay);

  void process();
  void grant();
};