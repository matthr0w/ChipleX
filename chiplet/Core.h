#pragma once

#include <systemc>

#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

SC_MODULE(Core) {
public:
  tlm_utils::simple_initiator_socket<Core> socket;

  SC_CTOR(Core);

private:
  void thread();

  sc_core::sc_event transactionFinished_event;

  tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload & trans,
                                     tlm::tlm_phase & phase,
                                     sc_core::sc_time & delay);
};