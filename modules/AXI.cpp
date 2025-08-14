#include "AXI.h"

#include "include/logging.h"

AXI::AXI(sc_module_name name, unsigned int read_channel_width,
                       unsigned int write_channel_width,
                       sc_time read_channel_clk_cycle,
                       sc_time write_channel_clk_cycle)
    : sc_module(name), read_channel_width(read_channel_width),
      write_channel_width(write_channel_width),
      read_channel_clk_cycle(read_channel_clk_cycle),
      write_channel_clk_cycle(write_channel_clk_cycle),
      read_target_socket("read_target_socket"),
      read_initiator_socket("read_initiator_socket"),
      write_target_socket("write_target_socket"),
      write_initiator_socket("write_initiator_socket"),
      peq_read_channel("peq_read_channel"),
      peq_write_channel("peq_write_channel") {
  read_target_socket.register_nb_transport_fw(
      this, &AXI::nb_transport_fw_read);
  read_initiator_socket.register_nb_transport_bw(
      this, &AXI::nb_transport_bw_read);

  write_target_socket.register_nb_transport_fw(
      this, &AXI::nb_transport_fw_write);
  write_initiator_socket.register_nb_transport_bw(
      this, &AXI::nb_transport_bw_write);

  SC_THREAD(process_read_transaction);
  sensitive << peq_read_channel.get_event();
  SC_THREAD(process_write_transaction);
  sensitive << peq_write_channel.get_event();

  SC_THREAD(process_read_channel);
  SC_THREAD(process_write_channel);
}

void AXI::process_read_transaction() {
  tlm_generic_payload *transaction;
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq_read_channel.get_next_transaction();
  }
}

void AXI::process_write_transaction() {
  tlm_generic_payload *transaction;
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq_write_channel.get_next_transaction();
  }
}

void AXI::process_read_channel() {}

void AXI::process_write_channel() {}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum AXI::nb_transport_fw_read(tlm_generic_payload &transaction,
                                               tlm_phase &phase,
                                               sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "PROTOCOL: Received request on read channel");

  phase = END_REQ;
  return TLM_UPDATED;
}

tlm_sync_enum
AXI::nb_transport_fw_write(tlm_generic_payload &transaction,
                                  tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction,
               "PROTOCOL: Received request on write channel");

  phase = END_REQ;
  return TLM_UPDATED;
}

tlm_sync_enum AXI::nb_transport_bw_read(tlm_generic_payload &transaction,
                                               tlm_phase &phase,
                                               sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction,
                 "PROTOCOL: Received response on read channel");

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum
AXI::nb_transport_bw_write(tlm_generic_payload &transaction,
                                  tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction,
                 "PROTOCOL: Received response on write channel");

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}