#include "AXISubordinate.h"

AXISubordinate::AXISubordinate(sc_module_name name,
                               unsigned int axi_channel_width,
                               sc_time axi_clk_cycle)
    : sc_module(name), axi_channel_width(axi_channel_width),
      axi_clk_cycle(axi_clk_cycle) {
  tsocket.register_nb_transport_fw(this, &AXISubordinate::nb_transport_fw);
  isocket.register_nb_transport_bw(this, &AXISubordinate::nb_transport_bw);

  SC_THREAD(process_read_channel);
  SC_THREAD(process_write_channel);
}

void AXISubordinate::process_read_channel() {
  while (true) {
    wait(read_req_evt);

    while (!read_channel.empty()) {
      Request read_request = read_channel.front();
      read_channel.pop_front();

      tlm_generic_payload *transaction = read_request.transaction;
      tlm_phase phase = *read_request.phase;
      sc_time delay = *read_request.delay;

      isocket->nb_transport_fw(*transaction, phase, delay);
    }
  }
}

void AXISubordinate::process_write_channel() {
  while (true) {
    wait(write_req_evt);

    while (!write_channel.empty()) {
      Request write_request = write_channel.front();
      write_channel.pop_front();

      tlm_generic_payload *transaction = write_request.transaction;
      tlm_phase phase = *write_request.phase;
      sc_time delay = *write_request.delay;

      isocket->nb_transport_fw(*transaction, phase, delay);
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
    delay += delays.axi_request(transaction);

    if (transaction.is_read()) {
      read_channel.push_back({&transaction, &phase, &delay});
      read_req_evt.notify(SC_ZERO_TIME);
    } else if (transaction.is_write()) {
      write_channel.push_back({&transaction, &phase, &delay});
      write_req_evt.notify(SC_ZERO_TIME);
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

  return tsocket->nb_transport_bw(transaction, phase, delay);
}