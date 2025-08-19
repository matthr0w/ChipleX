#include "AXISubordinate.h"

#include "common/Delays.h"

#include "include/logging.h"

AXISubordinate::AXISubordinate(sc_module_name name,
                               unsigned int read_channel_width,
                               unsigned int write_channel_width,
                               sc_time read_channel_clk_cycle,
                               sc_time write_channel_clk_cycle)
    : sc_module(name), read_channel_width(read_channel_width),
      write_channel_width(write_channel_width),
      read_channel_clk_cycle(read_channel_clk_cycle),
      write_channel_clk_cycle(write_channel_clk_cycle) {
  target_socket.register_nb_transport_fw(this,
                                         &AXISubordinate::nb_transport_fw);
  initiator_socket.register_nb_transport_bw(this,
                                            &AXISubordinate::nb_transport_bw);

  SC_THREAD(process_read_channel);
  SC_THREAD(process_write_channel);
}

void AXISubordinate::process_read_channel() {
  while (true) {
    wait(read_request_issued);

    while (!read_channel.empty()) {
      Request read_request = read_channel.front();
      read_channel.pop_front();

      tlm_generic_payload *transaction = read_request.transaction;
      tlm_phase phase = *read_request.phase;
      sc_time delay = *read_request.delay;

      initiator_socket->nb_transport_fw(*transaction, phase, delay);
    }
  }
}

void AXISubordinate::process_write_channel() {
  while (true) {
    wait(write_request_issued);

    while (!write_channel.empty()) {
      Request write_request = write_channel.front();
      write_channel.pop_front();

      tlm_generic_payload *transaction = write_request.transaction;
      tlm_phase phase = *write_request.phase;
      sc_time delay = *write_request.delay;

      initiator_socket->nb_transport_fw(*transaction, phase, delay);
    }
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum AXISubordinate::nb_transport_fw(tlm_generic_payload &transaction,
                                              tlm_phase &phase,
                                              sc_time &delay) {
  if (transaction.is_read()) {
    SC_LOG_DEBUG(this, transaction,
                 "TLM Protocol: " << phase << " - Read Channel");
  } else if (transaction.is_write()) {
    SC_LOG_DEBUG(this, transaction,
                 "TLM Protocol: " << phase << " - Write Channel");
  }

  switch (phase) {
  case BEGIN_REQ:
    delay += get_bus_transfer_fw_delay(*this, transaction, sc_time(5, SC_NS),
                                       32); // TODO: update delay handling

    if (transaction.is_read()) {
      read_channel.push_back({&transaction, &phase, &delay});
      read_request_issued.notify(SC_ZERO_TIME);
    } else if (transaction.is_write()) {
      write_channel.push_back({&transaction, &phase, &delay});
      write_request_issued.notify(SC_ZERO_TIME);
    }

    return TLM_ACCEPTED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum AXISubordinate::nb_transport_bw(tlm_generic_payload &transaction,
                                              tlm_phase &phase,
                                              sc_time &delay) {
  if (transaction.is_read()) {
    SC_LOG_DEBUG(this, transaction,
                 "TLM Protocol: " << phase << " - Read Channel");
  } else if (transaction.is_write()) {
    SC_LOG_DEBUG(this, transaction,
                 "TLM Protocol: " << phase << " - Write Channel");
  }

  return target_socket->nb_transport_bw(transaction, phase, delay);
}