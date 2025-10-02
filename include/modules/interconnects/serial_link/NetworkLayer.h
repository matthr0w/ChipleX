#pragma once

#include <systemc>
#include <tlm>

#include "common/System.h"

#include "modules/interconnects/serial_link/FifoIf.h"
#include "modules/interconnects/serial_link/Types.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(SLNetworkLayer) {
private:
  // -------------------------------------------------------
  // Config
  // -------------------------------------------------------
  const unsigned chiplet_id;
  const unsigned num_cores;
  const unsigned num_links;
  const unsigned axi_width;
  const unsigned num_credits;
  const unsigned force_send_thresh;

public:
  // -------------------------------------------------------
  // Signals
  // -------------------------------------------------------
  sc_in<bool> clk;

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

  SLNetworkLayer(sc_module_name name, unsigned chiplet_id,
                 ChipletConfig chiplet_config,
                 InterconnectConfig interconnect_config, unsigned num_cores);

private:
  // -------------------------------------------------------
  // Internal Declarations
  // -------------------------------------------------------
  enum ChannelState { CLEAR, REQ, ACK };

  ChannelState aw_state = CLEAR;
  ChannelState w_state = CLEAR;
  ChannelState b_state = CLEAR;
  ChannelState ar_state = CLEAR;
  ChannelState r_state = CLEAR;

  std::deque<ARM::AXI::Payload *> aw_queue;
  std::deque<ARM::AXI::Payload *> w_queue;
  std::deque<ARM::AXI::Payload *> b_queue;
  std::deque<ARM::AXI::Payload *> ar_queue;
  std::deque<ARM::AXI::Payload *> r_queue;

  unsigned w_beat_count = 0;
  unsigned r_beat_count = 0;

  void clk_posedge();

  void committer_thread();
  void sender_thread();

  // -------------------------------------------------------
  // Internal Signals
  // -------------------------------------------------------
  sc_signal<AxiTrans_t> axi_in_sig;
  AxiTrans_t axi_in_trans;
  sc_signal<AxiTrans_t> axi_out_sig;
  AxiTrans_t axi_out_trans;

  Payload_t *payload_out;
  ARM::AXI::Payload *payload_in;

  std::deque<ARM::AXI::Payload *> pending_read_responses;
  std::deque<ARM::AXI::Payload *> pending_write_responses;

  sc_signal<Committer::State> committer_state_q, committer_state_d;
  sc_signal<bool> aw_gnt, w_gnt, b_gnt, ar_gnt, r_gnt;
  sc_signal<bool> axis_reg_valid_in, axis_reg_ready_in;

  std::vector<unsigned> credits_out;
  std::vector<unsigned> credits_to_send;
  std::vector<bool> credit_to_send_force;

  unsigned entropy = 0;

  // -------------------------------------------------------
  // Events
  // -------------------------------------------------------
  sc_event update_event;

  // -------------------------------------------------------
  // Transport Functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);
  tlm_sync_enum nb_transport_bw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);

  // -------------------------------------------------------
  // Helper Functions
  // -------------------------------------------------------
  void clear_axi_state();
  void send_axi_beats();
  void send_axi_response(AxiTrans_t & trans, bool is_master);

  void increment_credits(int link_id, unsigned credit);
  void decrement_credits(int link_id);
};