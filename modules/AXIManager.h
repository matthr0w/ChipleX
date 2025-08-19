#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(AXIManager) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<AXIManager> target_socket;
  simple_initiator_socket<AXIManager> initiator_socket;

  AXIManager(sc_module_name name, unsigned int read_channel_width,
             unsigned int write_channel_width, sc_time read_channel_clk_cycle,
             sc_time write_channel_clk_cycle);

private:
  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int read_channel_width;
  const unsigned int write_channel_width;
  const sc_time read_channel_clk_cycle;
  const sc_time write_channel_clk_cycle;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);
};