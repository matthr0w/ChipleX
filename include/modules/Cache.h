#pragma once

#include <systemc>
#include <tlm>
#include <vector>

#include "ARM/TLM/arm_axi4.h"
#include "common/Statistics.h"
#include "setup/Types.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(Cache) {
  private:
	void end_of_simulation() override;

	// -------------------------------------------------------
	// Config
	// -------------------------------------------------------
	const unsigned chiplet_id;
	const unsigned axi_width;
	const unsigned cache_size;
	const unsigned cache_block_size;
	const unsigned cache_store_buffer_size;
	const sc_time  clk_cycle;

  public:
	// -------------------------------------------------------
	// Signals
	// -------------------------------------------------------
	sc_in<bool> clk;

	// -------------------------------------------------------
	// Sockets
	// -------------------------------------------------------
	ARM::AXI::SimpleTargetSocket<Cache>    tsocket;
	ARM::AXI::SimpleInitiatorSocket<Cache> isocket;

	Cache(sc_module_name name, unsigned chiplet_id, ChipletConfig chiplet_config);
	~Cache();

  private:
	// -------------------------------------------------------
	// Internal Declarations
	// -------------------------------------------------------
	StatManager &stats = StatManager::instance();

	enum ChannelState {
		CLEAR,
		REQ,
		ACK
	};

	ChannelState aw_state = CLEAR;
	ChannelState w_state  = CLEAR;
	ChannelState ar_state = CLEAR;

	std::deque<ARM::AXI::Payload *> aw_queue_in;
	std::deque<ARM::AXI::Payload *> w_queue_in;
	std::deque<ARM::AXI::Payload *> ar_queue_in;
	std::deque<ARM::AXI::Payload *> aw_queue_out;
	std::deque<ARM::AXI::Payload *> w_queue_out;
	std::deque<ARM::AXI::Payload *> ar_queue_out;

	ARM::AXI::Payload *b_outgoing = nullptr;
	ARM::AXI::Payload *r_outgoing = nullptr;

	unsigned w_beat_count_in  = 0;
	unsigned w_beat_count_out = 0;
	unsigned r_beat_count     = 0;

	struct CacheLine {
		bool                 valid = false;
		uint32_t             tag   = 0;
		std::vector<uint8_t> data;

		CacheLine() : data(32, 0) {}
	};

	std::vector<CacheLine> cache_lines;

	struct CacheLineRequest {
		CacheLine *line    = nullptr;
		uint32_t   address = 0;
		uint32_t   tag     = 0;
	};

	std::deque<CacheLineRequest>                              cache_line_requests;
	std::unordered_map<ARM::AXI::Payload *, CacheLineRequest> active_cache_line_requests;

	struct StoreBufferEntry {
		uint32_t             address;
		std::vector<uint8_t> data;
		bool                 valid   = false;
		bool                 ongoing = false;
	};

	std::deque<StoreBufferEntry> store_buffer;
	size_t                       store_buffer_head = 0;
	size_t                       store_buffer_tail = 0;

	uint8_t *beat_data;

	// State variables
	bool r_beat_ready = false;
	bool b_beat_ready = false;

	// Accumulators
	unsigned num_lines;
	unsigned num_accesses;
	unsigned num_hits;
	unsigned num_misses;

	void clk_posedge();
	void clk_negedge();

	// -------------------------------------------------------
	// Transport Functions
	// -------------------------------------------------------
	tlm_sync_enum nb_transport_fw(ARM::AXI::Payload & payload, ARM::AXI::Phase & phase);

	tlm_sync_enum nb_transport_bw(ARM::AXI::Payload & payload, ARM::AXI::Phase & phase);

	// -------------------------------------------------------
	// Helper Functions
	// -------------------------------------------------------
	uint32_t set_beat_address(uint32_t base, unsigned beat_idx, unsigned beat_bytes, unsigned beats,
	                          ARM::AXI::Burst burst);

	void enqueue_cacheline_read();
	void complete_cacheline_read(ARM::AXI::Payload * payload);

	void enqueue_storebuffer_write();
	void complete_storebuffer_write(ARM::AXI::Payload * payload);

	// -------------------------------------------------------
	// Debug Functions
	// -------------------------------------------------------
  public:
	void dump();
};