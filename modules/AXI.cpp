#include "AXI.h"

#include "common/Delays.h"

#include "include/logging.h"

AXI::AXI(sc_module_name name, unsigned int read_channel_width,
         unsigned int write_channel_width, sc_time read_channel_clk_cycle,
         sc_time write_channel_clk_cycle)
    : sc_module(name), read_channel_width(read_channel_width),
      write_channel_width(write_channel_width),
      read_channel_clk_cycle(read_channel_clk_cycle),
      write_channel_clk_cycle(write_channel_clk_cycle) {
  read_target_socket.register_nb_transport_fw(this, &AXI::nb_transport_fw_read);
  read_initiator_socket.register_nb_transport_bw(this,
                                                 &AXI::nb_transport_bw_read);

  SC_THREAD(process_read_channel);
  SC_THREAD(process_write_channel);
}

void AXI::process_read_channel() {
  while (true) {
    wait(read_request_issued);

    while (!read_channel.empty()) {
      Request read_request = read_channel.front();
      read_requests[read_request.transaction] = {read_request.module};
      read_channel.pop_front();

      tlm_generic_payload *transaction = read_request.transaction;
      tlm_phase phase = *read_request.phase;
      sc_time delay = *read_request.delay;

      read_initiator_socket->nb_transport_fw(*transaction, phase, delay);
    }
  }
}

void AXI::process_write_channel() {}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum AXI::nb_transport_fw_read(int id,
                                        tlm_generic_payload &transaction,
                                        tlm_phase &phase, sc_time &delay) {
  std::scoped_lock lock(read_request_mutex);

  SC_LOG_DEBUG(this, transaction,
               "TLM Protocol: " << phase << " - Read Channel - Module " << id);

  switch (phase) {
  case BEGIN_REQ:
    delay += get_bus_arbitration_delay(*this, transaction, sc_time(5, SC_NS));

    read_channel.push_back({id, &transaction, &phase, &delay});
    read_request_issued.notify(SC_ZERO_TIME);

    return TLM_ACCEPTED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum AXI::nb_transport_fw_write(int id,
                                         tlm_generic_payload &transaction,
                                         tlm_phase &phase, sc_time &delay) {
  return TLM_ACCEPTED;
}

tlm_sync_enum AXI::nb_transport_bw_read(tlm_generic_payload &transaction,
                                        tlm_phase &phase, sc_time &delay) {
  int id = read_requests.find(&transaction)->second;

  SC_LOG_DEBUG(this, transaction,
               "TLM Protocol: " << phase << " - Read Channel - Module " << id);

  switch (phase) {
  case END_REQ: {
    sc_time delay = sc_time(5, SC_NS);
    return read_target_socket[id]->nb_transport_bw(transaction, phase, delay);
  }
  case BEGIN_RESP:
    read_requests.erase(&transaction);
    return read_target_socket[id]->nb_transport_bw(transaction, phase, delay);
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum AXI::nb_transport_bw_write(tlm_generic_payload &transaction,
                                         tlm_phase &phase, sc_time &delay) {
  return TLM_ACCEPTED;
}