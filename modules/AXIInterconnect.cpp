#include "AXIInterconnect.h"

AXIInterconnect::AXIInterconnect(sc_module_name name, unsigned int chip_id,
                                 unsigned int num_managers,
                                 unsigned int num_subordinates,
                                 sc_time axi_clk_cycle,
                                 sc_time axi_arbitration_delay)
    : sc_module(name), chip_id(chip_id), axi_clk_cycle(axi_clk_cycle),
      axi_arbitration_delay(axi_arbitration_delay) {
  tsockets = new simple_target_socket_tagged<AXIInterconnect>[num_managers];
  isockets =
      new simple_initiator_socket_tagged<AXIInterconnect>[num_subordinates];

  for (unsigned int i = 0; i < num_managers; ++i) {
    tsockets[i].register_nb_transport_fw(this,
                                         &AXIInterconnect::nb_transport_fw, i);
  }

  for (unsigned int i = 0; i < num_subordinates; ++i) {
    isockets[i].register_nb_transport_bw(this,
                                         &AXIInterconnect::nb_transport_bw, i);
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum AXIInterconnect::nb_transport_fw(int id,
                                               tlm_generic_payload &transaction,
                                               tlm_phase &phase,
                                               sc_time &delay) {
  std::scoped_lock lock(request_mutex);

  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase);

  ChipletExtension *ext;
  transaction.get_extension(ext);

  switch (phase) {
  case BEGIN_REQ:
    requests_map[&transaction] = id;

    delay += delays.axi_arbitration(transaction);

    int tmodule = (ext->destination_id == chip_id) ? 0 : 1;
    return isockets[tmodule]->nb_transport_fw(transaction, phase, delay);
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum AXIInterconnect::nb_transport_bw(int id,
                                               tlm_generic_payload &transaction,
                                               tlm_phase &phase,
                                               sc_time &delay) {
  int imodule = requests_map.find(&transaction)->second;

  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase);

  switch (phase) {
  case END_REQ: {
    sc_time delay = delays.axi_address(transaction);
    return tsockets[imodule]->nb_transport_bw(transaction, phase, delay);
  }
  case BEGIN_RESP:
    requests_map.erase(&transaction);
    return tsockets[imodule]->nb_transport_bw(transaction, phase, delay);
  }

  return TLM_ACCEPTED;
}