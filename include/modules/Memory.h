#pragma once

#include <map>
#include <optional>
#include <systemc>
#include <tlm>
#include <unordered_map>
#include <vector>

#include "ARM/TLM/arm_axi4.h"
#include "common/Statistics.h"
#include "setup/Types.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(Memory) {
  private:
	void end_of_simulation() override;

	// -------------------------------------------------------
	// Config
	// -------------------------------------------------------
	const unsigned axi_width;
	const unsigned size;
	const sc_time  clk_cycle;
	const unsigned access_latency;

  public:
	// -------------------------------------------------------
	// Signals
	// -------------------------------------------------------
	sc_in<bool> clk;

	// -------------------------------------------------------
	// Sockets
	// -------------------------------------------------------
	ARM::AXI::SimpleTargetSocket<Memory> tsocket;

	Memory(sc_module_name name, ChipletConfig chiplet_config);
	~Memory();

	// Direct local access with no bus traffic and no time cost; addressing and
	// range lifetime match the AXI path.
	uint32_t local_write(const unsigned char *data, unsigned length, std::optional<uint32_t> address = std::nullopt);
	void     local_read(unsigned char *data, unsigned length, uint32_t address);

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

	ChannelState b_state = CLEAR;
	ChannelState r_state = CLEAR;

	std::deque<ARM::AXI::Payload *> aw_queue;
	std::deque<ARM::AXI::Payload *> w_queue;
	std::deque<ARM::AXI::Payload *> ar_queue;

	ARM::AXI::Payload *b_outgoing = nullptr;
	ARM::AXI::Payload *r_outgoing = nullptr;

	unsigned r_beat_count;

	// Memory
	enum class MemoryState {
		Idle,
		WriteSet,
		ReadSet,
		WriteAccess,
		ReadAccess,
		WriteResponse,
		ReadResponse
	};

	std::vector<uint8_t>         mem;
	std::vector<uint8_t>         mem_bitmap;
	MemoryState                  mem_state   = MemoryState::Idle;
	unsigned                     access_wait = 0;
	std::map<uint32_t, unsigned> allocated_ranges;
	uint32_t                     offchip_base_address;
	uint32_t                     active_addr = 0;

	void clk_posedge();
	void clk_negedge();

	// -------------------------------------------------------
	// Transport Functions
	// -------------------------------------------------------
	tlm_sync_enum nb_transport_fw(ARM::AXI::Payload & payload, ARM::AXI::Phase & phase);

	// -------------------------------------------------------
	// Debug Functions
	// -------------------------------------------------------
	// Subordinate-side AXI trace. A memory chiplet has no bus to log phases.
	void trace_axi(ARM::AXI::Payload & payload, ARM::AXI::Phase phase);

	uint8_t                                                *beat_data;
	std::unordered_map<const ARM::AXI::Payload *, unsigned> trace_beat_index;

	// -------------------------------------------------------
	// Helper Functions
	// -------------------------------------------------------
	void set_active_address(ARM::AXI::Payload & payload);

	uint32_t set_beat_address(uint32_t base, unsigned beat_idx, unsigned beat_bytes, unsigned beats,
	                          ARM::AXI::Burst burst);

	uint32_t allocate_dynamic_address(bool onchip, unsigned size);
	void     deallocate_dynamic_address(uint32_t address, unsigned size);
};