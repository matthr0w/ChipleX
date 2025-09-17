#pragma once

#include <systemc>
#include <tlm>

#include "modules/interconnects/serial_link/FifoIf.h"
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
  sc_port<FifoIf> stream_fifo_in;
  sc_port<FifoIf> stream_fifo_out;

  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  // AXI slave port
  ARM::AXI::SimpleTargetSocket<SLNetworkLayer> axi_in;
  // AXI master port
  ARM::AXI::SimpleInitiatorSocket<SLNetworkLayer> axi_out;

  SLNetworkLayer(sc_module_name name, unsigned axi_width, int num_credits);

private:
  enum ChannelState { CLEAR, REQ, ACK };

  ChannelState aw_state = CLEAR;
  ChannelState w_state = CLEAR;
  ChannelState b_state = CLEAR;
  ChannelState ar_state = CLEAR;

  std::deque<ARM::AXI::Payload *> aw_queue;
  std::deque<ARM::AXI::Payload *> w_queue;
  std::deque<ARM::AXI::Payload *> b_queue;
  std::deque<ARM::AXI::Payload *> ar_queue;

  unsigned w_beat_count = 0;

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

  Payload_t *payload_out;
  ARM::AXI::Payload *payload_in;

  std::deque<ARM::AXI::Payload *> pending_write_responses;

  sc_signal<Committer::State> committer_state_q, committer_state_d;
  sc_signal<bool> entropy_q, entropy_d;
  sc_signal<bool> aw_gnt, w_gnt, b_gnt, ar_gnt, r_gnt;
  sc_signal<bool> axis_reg_valid_in, axis_reg_ready_in;

  unsigned credits_out = num_credits;
  unsigned credits_to_send = 0;
  bool credit_to_send_force = false;
  bool credit_received = false;

  uint64_t latest_user = 0;

  // -------------------------------------------------------
  // Events
  // -------------------------------------------------------
  sc_event update_event;

  // -------------------------------------------------------
  // Transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);
  tlm_sync_enum nb_transport_bw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);
};