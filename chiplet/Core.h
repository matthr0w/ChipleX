#pragma once

#include <systemc>

#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

SC_MODULE(Core) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  tlm_utils::simple_initiator_socket<Core> socket;
  tlm_utils::simple_target_socket<Core> irq_socket;

  Core(sc_core::sc_module_name name, unsigned int chiplet_id,
       unsigned int core_id);

private:
  const unsigned int chiplet_id;
  const unsigned int core_id;

  bool running;

  unsigned int request;

  void core_thread();
  void send_request(tlm::tlm_command command, int request_id,
                    int destination_id, uint32_t address, unsigned char *data,
                    unsigned int data_size);

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_core::sc_event transaction_done;

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  tlm_utils::peq_with_get<tlm::tlm_generic_payload> irq_peq;
  void handle_interrupt();

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm::tlm_sync_enum nb_transport_fw_irq(tlm::tlm_generic_payload & transaction,
                                         tlm::tlm_phase & phase,
                                         sc_core::sc_time & delay);
  tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload & transaction,
                                     tlm::tlm_phase & phase,
                                     sc_core::sc_time & delay);
};