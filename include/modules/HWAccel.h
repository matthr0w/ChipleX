#pragma once

#include <optional>
#include <systemc>
#include <tlm>

#include "ARM/TLM/arm_axi4.h"
#include "common/Requests.h"
#include "common/Statistics.h"
#include "modules/DMAEngine.h"
#include "setup/Types.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(HWAccel), public DMAForwardInterface {
  private:
	// -------------------------------------------------------
	// Config
	// -------------------------------------------------------
	const unsigned chiplet_id;
	const unsigned axi_width;
	const CyclesDB cycles;
	const sc_time  clk_cycle;

  public:
	// -------------------------------------------------------
	// Signals
	// -------------------------------------------------------
	sc_in<bool> clk;

	// -------------------------------------------------------
	// Sockets
	// -------------------------------------------------------
	ARM::AXI::SimpleTargetSocket<HWAccel> tsocket;

	HWAccel(sc_module_name name, unsigned chiplet_id, ChipletConfig chiplet_config, const CyclesDB &cycles,
	        DMAEngine *dma_engine);

	void main_thread();

	std::function<void(HWAccel &, uint8_t *, size_t)> main_fn;

	// -------------------------------------------------------
	// Program API
	// -------------------------------------------------------
	unsigned MAX_INCR_BURST_SIZE  = 0;
	unsigned MAX_FIXED_BURST_SIZE = 0;
	unsigned MAX_WRAP_BURST_SIZE  = 0;

	void wait_cycles(const std::string &name);

	// -------------------------------------------------------
	// AXI API
	// -------------------------------------------------------
  private:
	std::shared_ptr<RequestHandle> read_internal(
	    uint32_t request_id, uint8_t src_module, uint8_t dst_chiplet, uint8_t dst_module, uint32_t address,
	    bool fixed_address, unsigned char *data, unsigned data_length, ARM::AXI::Burst burst, uint8_t extension_mask);
	std::shared_ptr<RequestHandle> write_internal(
	    uint32_t request_id, uint8_t src_module, uint8_t dst_chiplet, uint8_t dst_module, uint32_t address,
	    bool fixed_address, unsigned char *data, unsigned data_length, ARM::AXI::Burst burst, uint8_t extension_mask);

  public:
	std::shared_ptr<RequestHandle> read(const AxiRequest &req);
	std::shared_ptr<RequestHandle> write(const AxiRequest &req);

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
	ChannelState b_state  = CLEAR;
	ChannelState ar_state = CLEAR;

	std::deque<ARM::AXI::Payload *> aw_queue_in;
	std::deque<ARM::AXI::Payload *> aw_queue_out;
	std::deque<ARM::AXI::Payload *> w_queue_in;
	std::deque<ARM::AXI::Payload *> w_queue_out;
	std::deque<ARM::AXI::Payload *> ar_queue_out;

	ARM::AXI::Payload *b_outgoing = nullptr;

	unsigned w_beat_count = 0;

	std::unordered_map<ARM::AXI::Payload *, std::shared_ptr<RequestHandle>> request_handles;

	std::deque<ARM::AXI::Payload *> data_queue;

	enum class AccelState {
		Idle,
		Busy
	};
	AccelState state = AccelState::Idle;

	// DMA engine
	DMAEngine *dma_engine = nullptr;
	int        dma_vm_id  = -1;

	void clk_posedge();
	void clk_negedge();

	// -------------------------------------------------------
	// Events
	// -------------------------------------------------------
	sc_event read_done;
	sc_event write_done;
	sc_event data_request;

	// -------------------------------------------------------
	// Transport Functions
	// -------------------------------------------------------
	tlm_sync_enum nb_transport_fw(ARM::AXI::Payload & payload, ARM::AXI::Phase & phase);
	tlm_sync_enum nb_transport_bw_axi(ARM::AXI::Payload & payload, ARM::AXI::Phase & phase);

	// -------------------------------------------------------
	// Helper Functions
	// -------------------------------------------------------
	bool send_dma_request(ARM::AXI::Payload & payload, ARM::AXI4::Phase phase) {
		return dma_engine->forward_from_virtual(dma_vm_id, payload, phase);
	}
};