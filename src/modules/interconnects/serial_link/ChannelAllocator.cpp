#include "modules/interconnects/serial_link/ChannelAllocator.h"

SLChannelAllocater::SLChannelAllocater(sc_module_name name, double distance)
    : sc_module(name), distance(distance) {
  data_in_tsocket.register_nb_transport_fw(
      this, &SLChannelAllocater::nb_transport_fw_data_in, 0);
  data_in_isocket.register_nb_transport_bw(
      this, &SLChannelAllocater::nb_transport_bw_data_in);
  data_out_tsocket.register_nb_transport_fw(
      this, &SLChannelAllocater::nb_transport_fw_data_out);
  data_out_isocket.register_nb_transport_bw(
      this, &SLChannelAllocater::nb_transport_bw_data_out, 0);
}

// -------------------------------------------------------
// Transport functions
// -------------------------------------------------------
tlm_sync_enum
SLChannelAllocater::nb_transport_fw_data_in(int id,
                                            tlm_generic_payload &transaction,
                                            tlm_phase &phase, sc_time &delay) {
  delay += delays.transfer_delay(transaction);
  return data_in_isocket->nb_transport_fw(transaction, phase, delay);
}

tlm_sync_enum
SLChannelAllocater::nb_transport_bw_data_in(tlm_generic_payload &transaction,
                                            tlm_phase &phase, sc_time &delay) {
  return data_in_tsocket->nb_transport_bw(transaction, phase, delay);
}

tlm_sync_enum
SLChannelAllocater::nb_transport_fw_data_out(tlm_generic_payload &transaction,
                                             tlm_phase &phase, sc_time &delay) {
  return data_out_isocket->nb_transport_fw(transaction, phase, delay);
}

tlm_sync_enum
SLChannelAllocater::nb_transport_bw_data_out(int id,
                                             tlm_generic_payload &transaction,
                                             tlm_phase &phase, sc_time &delay) {
  return data_out_tsocket->nb_transport_bw(transaction, phase, delay);
}