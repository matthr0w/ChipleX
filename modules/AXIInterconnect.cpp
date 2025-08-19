#include "AXIInterconnect.h"

#include "common/Delays.h"

#include "include/logging.h"

AXIInterconnect::AXIInterconnect(sc_module_name name, unsigned int chip_id,
                                 unsigned int num_managers,
                                 unsigned int num_subordinates,
                                 unsigned int read_channel_width,
                                 unsigned int write_channel_width,
                                 sc_time read_channel_clk_cycle,
                                 sc_time write_channel_clk_cycle)
    : sc_module(name), chip_id(chip_id), read_channel_width(read_channel_width),
      write_channel_width(write_channel_width),
      read_channel_clk_cycle(read_channel_clk_cycle),
      write_channel_clk_cycle(write_channel_clk_cycle) {
  target_sockets =
      new simple_target_socket_tagged<AXIInterconnect>[num_managers];
  initiator_sockets =
      new simple_initiator_socket_tagged<AXIInterconnect>[num_subordinates];

  for (unsigned int i = 0; i < num_managers; ++i) {
    target_sockets[i].register_nb_transport_fw(
        this, &AXIInterconnect::nb_transport_fw, i);
  }

  for (unsigned int i = 0; i < num_subordinates; ++i) {
    initiator_sockets[i].register_nb_transport_bw(
        this, &AXIInterconnect::nb_transport_bw, i);
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

  int module = (ext->destination_id == chip_id) ? 0 : 1;

  switch (phase) {
  case BEGIN_REQ:
    requests_map[&transaction] = module;

    delay += get_bus_arbitration_delay(
        *this, transaction, sc_time(5, SC_NS)); // TODO: update delay handling

    return initiator_sockets[module]->nb_transport_fw(transaction, phase,
                                                      delay);
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum AXIInterconnect::nb_transport_bw(int id,
                                               tlm_generic_payload &transaction,
                                               tlm_phase &phase,
                                               sc_time &delay) {
  int module = requests_map.find(&transaction)->second;

  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase);

  switch (phase) {
  case END_REQ: {
    sc_time delay = sc_time(5, SC_NS); // TODO: update delay handling
    return target_sockets[module]->nb_transport_bw(transaction, phase, delay);
  }
  case BEGIN_RESP:
    requests_map.erase(&transaction);
    return target_sockets[module]->nb_transport_bw(transaction, phase, delay);
  }

  return TLM_ACCEPTED;
}