#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

namespace fpga {
SC_MODULE(Generator) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_initiator_socket<Generator> socket;
  simple_target_socket<Generator> irq_socket;

  Generator(sc_module_name name, unsigned int fpga_id);

private:
  const unsigned int fpga_id;

  sc_mutex request_mutex;
  unsigned int request;

  void gen_thread();
  void send_request(tlm_command command, int request_id,
                    int destination_id, uint32_t address, unsigned char *data,
                    unsigned int data_size);

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event transaction_done;

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm_generic_payload> irq_peq;
  void handle_interrupt();

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_irq(tlm_generic_payload & transaction,
                                         tlm_phase & phase,
                                         sc_time & delay);
  tlm_sync_enum nb_transport_bw(tlm_generic_payload & transaction,
                                     tlm_phase & phase,
                                     sc_time & delay);
};
}; // namespace fpga