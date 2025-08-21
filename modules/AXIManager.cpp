#include "AXIManager.h"

AXIManager::AXIManager(sc_module_name name, unsigned int axi_channel_width,
                       sc_time axi_clk_cycle)
    : sc_module(name), axi_channel_width(axi_channel_width),
      axi_clk_cycle(axi_clk_cycle) {
  tsocket.register_nb_transport_fw(this, &AXIManager::nb_transport_fw);
  isocket.register_nb_transport_bw(this, &AXIManager::nb_transport_bw);
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum AXIManager::nb_transport_fw(tlm_generic_payload &transaction,
                                          tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase);

  switch (phase) {
  case BEGIN_REQ:
    return isocket->nb_transport_fw(transaction, phase, delay);
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum AXIManager::nb_transport_bw(tlm_generic_payload &transaction,
                                          tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase);

  switch (phase) {
  case BEGIN_RESP:
    delay += delays.axi_response(transaction);
    break;
  }

  return tsocket->nb_transport_bw(transaction, phase, delay);
}