#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(AXI) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<AXI> read_target_socket;
  simple_initiator_socket<AXI> read_initiator_socket;
  simple_target_socket<AXI> write_target_socket;
  simple_initiator_socket<AXI> write_initiator_socket;

  AXI(sc_module_name name, unsigned int read_channel_width,
             unsigned int write_channel_width, sc_time read_channel_clk_cycle,
             sc_time write_channel_clk_cycle);

private:
  void process_read_channel();
  void process_write_channel();

  std::deque<tlm_generic_payload *> read_channel;
  std::deque<tlm_generic_payload *> write_channel;

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int read_channel_width;
  const unsigned int write_channel_width;
  const sc_time read_channel_clk_cycle;
  const sc_time write_channel_clk_cycle;

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm_generic_payload> peq_read_channel;
  void process_read_transaction();
  peq_with_get<tlm_generic_payload> peq_write_channel;
  void process_write_transaction();

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event read_transaction_done;
  sc_event write_transaction_done;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_read(tlm_generic_payload & transaction,
                                     tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw_read(tlm_generic_payload & transaction,
                                      tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_fw_write(
      tlm_generic_payload & transaction, tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw_write(
      tlm_generic_payload & transaction, tlm_phase & phase, sc_time & delay);
};