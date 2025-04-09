#pragma once

#include <systemc>

#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

SC_MODULE(Core) {
public:
  tlm_utils::simple_initiator_socket<Core> socket;

  SC_CTOR(Core);

private:
  sc_core::sc_event transaction_done;

  tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload & trans,
                                     tlm::tlm_phase & phase,
                                     sc_core::sc_time & delay);

  void thread();
  void send_request(tlm::tlm_command command, uint32_t address,
                    unsigned char *data, unsigned int data_size);
};