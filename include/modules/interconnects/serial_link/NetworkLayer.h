#pragma once

#include <deque>
#include <map>
#include <memory>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "modules/interconnects/serial_link/FifoIf.h"
#include "modules/interconnects/serial_link/Types.h"
#include "setup/Types.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(SLNetworkLayer) {
  private:
	// -------------------------------------------------------
	// Config
	// -------------------------------------------------------
	// InterconnectBase
	const unsigned chiplet_id;
	const unsigned interconnect_id;
	const unsigned num_links;
	const unsigned num_cores;
	const unsigned axi_width;
	// YAML
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
	// IRQ sockets
	simple_initiator_socket_tagged<SLNetworkLayer> *irq_sockets;

	SLNetworkLayer(sc_module_name name, unsigned chiplet_id, unsigned interconnect_id,
	               InterconnectConfig interconnect_config, unsigned num_links, unsigned num_cores, unsigned axi_width);
	~SLNetworkLayer();

  private:
	// -------------------------------------------------------
	// Internal Declarations
	// -------------------------------------------------------
	enum ChannelState {
		CLEAR,
		REQ,
		ACK
	};

	ChannelState aw_state = CLEAR;
	ChannelState w_state  = CLEAR;
	ChannelState b_state  = CLEAR;
	ChannelState ar_state = CLEAR;
	ChannelState r_state  = CLEAR;

	std::deque<ARM::AXI::Payload *> aw_queue;
	std::deque<ARM::AXI::Payload *> w_queue;
	std::deque<ARM::AXI::Payload *> b_queue;
	std::deque<ARM::AXI::Payload *> ar_queue;
	std::deque<ARM::AXI::Payload *> r_queue;

	unsigned w_beat_count = 0;
	unsigned r_beat_count = 0;

	// Set once the last beat of a read response has been accepted by the local
	// bus. A read cannot be tracked on the request channel the way a write is:
	// its data returns over the link, not through axi_in.
	bool r_response_done = false;

	void clk_posedge();

	void committer_thread();
	void sender_thread();

	// -------------------------------------------------------
	// Internal Signals
	// -------------------------------------------------------
	sc_signal<AxiTrans_t> axi_in_sig;
	AxiTrans_t            axi_in_trans;
	sc_signal<AxiTrans_t> axi_out_sig;
	AxiTrans_t            axi_out_trans;

	Payload_t *payload_out = nullptr;

	// Inbound: remote requests from the links are served one at a time on the
	// single local axi_out port. Writes are reassembled per source chiplet
	// (sources interleave on the shared FIFO); the rest wait their turn. A
	// source may have several writes outstanding, so they are held in arrival
	// order and each beat is routed to the matching payload of its source.
	struct InboundWrite {
		ARM::AXI::Payload *payload        = nullptr;
		unsigned           beats_buffered = 0; // arrived before this write became active
		unsigned           beats_received = 0; // beats reassembled into the payload so far
		uint8_t            src            = 0;
	};

	std::deque<std::shared_ptr<InboundWrite>> inbound_write_queue; // front is driven, rest wait
	bool                                      inbound_write_active = false;
	bool                                      inbound_write_done   = false;

	std::deque<ARM::AXI::Payload *> inbound_read_wait;
	bool                            inbound_read_active = false;
	bool                            inbound_read_done   = false;

	// Write responses from the local subordinate: queued on _out, then moved to
	// _ack once placed on the link and acknowledged the next cycle.
	std::deque<ARM::AXI::Payload *> inbound_b_out;
	std::deque<ARM::AXI::Payload *> inbound_b_ack;

	std::deque<ARM::AXI::Payload *> pending_read_responses;
	std::deque<ARM::AXI::Payload *> pending_write_responses;

	// Outbound: local managers drive the single axi_in request register. Extra
	// requests wait while it is occupied; beats driven before a request is
	// installed are deferred so they do not overwrite the active write.
	std::deque<ARM::AXI::Payload *>                outbound_write_wait;
	std::deque<ARM::AXI::Payload *>                outbound_read_wait;
	std::map<ARM::AXI::Payload *, ARM::AXI::Phase> outbound_deferred_beats;

	sc_signal<Committer::State> committer_state_q, committer_state_d;
	sc_signal<bool>             aw_gnt, w_gnt, b_gnt, ar_gnt, r_gnt;
	sc_signal<bool>             axis_reg_valid_in, axis_reg_ready_in;

	std::vector<unsigned> credits_out;
	std::vector<unsigned> credits_to_send;
	std::vector<bool>     credit_to_send_force;

	unsigned entropy = 0;

	// -------------------------------------------------------
	// Events
	// -------------------------------------------------------
	sc_event update_event;

	// -------------------------------------------------------
	// Transport Functions
	// -------------------------------------------------------
	tlm_sync_enum nb_transport_fw(ARM::AXI::Payload & payload, ARM::AXI::Phase & phase);
	tlm_sync_enum nb_transport_bw(ARM::AXI::Payload & payload, ARM::AXI::Phase & phase);

	// -------------------------------------------------------
	// Helper Functions
	// -------------------------------------------------------
	void clear_axi_state();
	void send_axi_beats();
	void send_axi_response(AxiTrans_t & trans, bool is_master);

	// Outbound: install a queued local request into the axi_in register and
	// promote the next waiting one once it frees.
	bool outbound_write_busy();
	bool outbound_read_busy();
	bool outbound_write_idle();
	void activate_outbound_write(ARM::AXI::Payload & payload);
	void activate_outbound_read(ARM::AXI::Payload & payload);
	void advance_outbound();

	// Inbound: give the local axi_out port to a remote request and promote the
	// next waiting one once the active transaction is answered.
	std::shared_ptr<InboundWrite> fill_target(uint8_t src_chiplet);
	void                          activate_inbound_write();
	void                          activate_inbound_read();
	void                          advance_inbound();

	void send_irq(ARM::AXI::Payload & payload);

	void increment_credits(int link_id, unsigned credit);
	void decrement_credits(int link_id);
};