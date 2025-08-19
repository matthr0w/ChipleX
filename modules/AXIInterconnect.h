#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(AXIInterconnect) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket_tagged<AXIInterconnect> *target_sockets;
  simple_initiator_socket_tagged<AXIInterconnect> *initiator_sockets;

  AXIInterconnect(
      sc_module_name name, unsigned int chip_id, unsigned int num_managers,
      unsigned int num_subordinates, unsigned int read_channel_width,
      unsigned int write_channel_width, sc_time read_channel_clk_cycle,
      sc_time write_channel_clk_cycle);

private:
  sc_mutex request_mutex;
  std::unordered_map<tlm::tlm_generic_payload *, int> requests_map;

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int chip_id;
  const unsigned int read_channel_width;
  const unsigned int write_channel_width;
  const sc_time read_channel_clk_cycle;
  const sc_time write_channel_clk_cycle;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(int id, tlm_generic_payload &transaction,
                                tlm_phase &phase, sc_time &delay);

  tlm_sync_enum nb_transport_bw(int id, tlm_generic_payload &transaction,
                                tlm_phase &phase, sc_time &delay);
};