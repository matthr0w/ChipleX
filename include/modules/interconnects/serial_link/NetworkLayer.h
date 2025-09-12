#pragma once

#include <systemc>
#include <tlm>

#include "modules/interconnects/serial_link/Types.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(SLNetworkLayer) {
public:
  // -------------------------------------------------------
  // Signals
  // -------------------------------------------------------
  sc_in<bool> clk;
  sc_in<bool> rst_n;

  // -------------------------------------------------------
  // Ports
  // -------------------------------------------------------
  sc_fifo_out<Payload_t *> stream_fifo_out;

  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  // AXI slave port
  ARM::AXI::SimpleTargetSocket<SLNetworkLayer> axi_in;
  // AXI master port
  // TODO: Implement master port
  // ARM::AXI::SimpleInitiatorSocket<SLNetworkLayer> axi_out;

  SLNetworkLayer(sc_module_name name, unsigned axi_width, int num_credits);

private:
  void clk_posedge();

  void committer_thread();
  void sender_thread();

  // -------------------------------------------------------
  // Parameters
  // -------------------------------------------------------
  const unsigned axi_width;
  const int num_credits;
  const int force_send_thresh;

  // -------------------------------------------------------
  // Internal signals
  // -------------------------------------------------------
  sc_signal<AxiTrans_t> axi_in_sig;
  AxiTrans_t axi_in_trans;
  sc_signal<AxiTrans_t> axi_out_sig;
  AxiTrans_t axi_out_trans;

  Payload_t *payload_out, *payload_in;

  sc_signal<Committer::State> committer_state_q, committer_state_d;

  sc_signal<bool> entropy_q, entropy_d;

  sc_signal<bool> aw_gnt, w_gnt, b_gnt, ar_gnt, r_gnt;

  sc_signal<bool> axis_reg_valid_in, axis_reg_ready_in;
  sc_signal<bool> axis_reg_valid_out, axis_reg_ready_out;

  sc_signal<int> credits_out;
  sc_signal<int> credits_to_send;
  sc_signal<bool> credit_to_send_force;

  bool comb_logic_updated = false; // Track logic updates to not respond twice

  // -------------------------------------------------------
  // Events
  // -------------------------------------------------------
  sc_event axi_in_event;

  // -------------------------------------------------------
  // Transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);
};