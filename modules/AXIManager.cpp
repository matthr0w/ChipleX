#include "AXIManager.h"

#include "common/Delays.h"

#include "include/logging.h"

AXIManager::AXIManager(sc_module_name name, unsigned int read_channel_width,
                       unsigned int write_channel_width,
                       sc_time read_channel_clk_cycle,
                       sc_time write_channel_clk_cycle)
    : sc_module(name), read_channel_width(read_channel_width),
      write_channel_width(write_channel_width),
      read_channel_clk_cycle(read_channel_clk_cycle),
      write_channel_clk_cycle(write_channel_clk_cycle) {
  target_socket.register_nb_transport_fw(this, &AXIManager::nb_transport_fw);
  initiator_socket.register_nb_transport_bw(this, &AXIManager::nb_transport_bw);
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum AXIManager::nb_transport_fw(tlm_generic_payload &transaction,
                                          tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase);

  switch (phase) {
  case BEGIN_REQ:
    return initiator_socket->nb_transport_fw(transaction, phase, delay);
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum AXIManager::nb_transport_bw(tlm_generic_payload &transaction,
                                          tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase);

  switch (phase) {
  case BEGIN_RESP:
    delay += get_bus_transfer_bw_delay(*this, transaction, sc_time(5, SC_NS),
                                       32); // TODO: update delay
  }

  return target_socket->nb_transport_bw(transaction, phase, delay);
}