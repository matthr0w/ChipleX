#pragma once

#include <systemc>

#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

SC_MODULE(Core) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  tlm_utils::simple_initiator_socket<Core> socket;

  Core(sc_core::sc_module_name name, unsigned int chiplet_instances);

private:
  const unsigned int chiplet_instances;

  void run_core();
  void send_request(tlm::tlm_command command, uint32_t address,
                    unsigned char *data, unsigned int data_size);

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_core::sc_event transaction_done;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload & trans,
                                     tlm::tlm_phase & phase,
                                     sc_core::sc_time & delay);
};