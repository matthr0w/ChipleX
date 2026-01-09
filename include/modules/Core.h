#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "logging.h"

#include "ARM/TLM/arm_axi4.h"
#include "common/IRQ.h"
#include "common/Requests.h"
#include "common/Statistics.h"
#include "setup/Types.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Core) {
private:
  // -------------------------------------------------------
  // Config
  // -------------------------------------------------------
  const unsigned chiplet_id;
  const unsigned core_id;
  const unsigned axi_width;
  const CyclesDB cycles;
  const sc_time clk_cycle;
  const sc_time irq_delay;

public:
  // -------------------------------------------------------
  // Signals
  // -------------------------------------------------------
  sc_in<bool> clk;

  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  ARM::AXI::SimpleInitiatorSocket<Core> isocket;
  simple_target_socket_tagged<Core> *irq_sockets;

  Core(sc_module_name name, unsigned chiplet_id, unsigned core_id,
       ChipletConfig chiplet_config, const CyclesDB &cycles, unsigned num_irqs);
  ~Core();

  void main_thread();
  void interrupt_thread();

  std::function<void(Core &)> main_fn;
  std::function<void(Core &, const IRQ &)> interrupt_fn;

  // -------------------------------------------------------
  // Program API
  // -------------------------------------------------------
  unsigned MAX_INCR_BURST_SIZE = 0;
  unsigned MAX_FIXED_BURST_SIZE = 0;
  unsigned MAX_WRAP_BURST_SIZE = 0;

  void wait_cycles(const std::string &name);

  // -------------------------------------------------------
  // AXI API
  // -------------------------------------------------------
private:
  std::shared_ptr<RequestHandle> read_internal(
      uint32_t request_id, uint8_t src_module, uint8_t dst_chiplet,
      uint8_t dst_module, uint32_t address, bool fixed_address,
      unsigned char *data, unsigned data_length, ARM::AXI::Burst burst,
      uint8_t extension_mask, bool is_volatile);
  std::shared_ptr<RequestHandle> write_internal(
      uint32_t request_id, uint8_t src_module, uint8_t dst_chiplet,
      uint8_t dst_module, uint32_t address, bool fixed_address,
      unsigned char *data, unsigned data_length, ARM::AXI::Burst burst,
      uint8_t extension_mask, bool is_volatile);
  std::shared_ptr<RequestHandle> dma_internal(
      uint32_t request_id, uint8_t src_fetch_module, uint8_t src_target_module,
      uint8_t fetch_chiplet, uint8_t fetch_module, uint8_t target_chiplet,
      uint8_t target_module, uint32_t fetch_addr, uint32_t target_addr,
      unsigned data_length, ARM::AXI::Burst burst, bool is_volatile);

public:
  std::shared_ptr<RequestHandle> read(const AxiRequest &req);
  std::shared_ptr<RequestHandle> write(const AxiRequest &req);
  std::shared_ptr<RequestHandle> dma(const AxiDMARequest &req);

private:
  // -------------------------------------------------------
  // Internal Declarations
  // -------------------------------------------------------
  StatManager &stats = StatManager::instance();

  enum ChannelState { CLEAR, REQ, ACK };

  ChannelState aw_state = CLEAR;
  ChannelState w_state = CLEAR;
  ChannelState ar_state = CLEAR;

  std::deque<ARM::AXI::Payload *> aw_queue;
  std::deque<ARM::AXI::Payload *> w_queue;
  std::deque<ARM::AXI::Payload *> ar_queue;

  unsigned w_beat_count = 0;

  std::unordered_map<ARM::AXI::Payload *, std::shared_ptr<RequestHandle>>
      request_handles;

  std::deque<tlm_generic_payload *> irq_queue;

  void clk_posedge();
  void clk_negedge();

  // -------------------------------------------------------
  // Delay Model
  // -------------------------------------------------------
  struct DelayModel {
  private:
    const Core &module;

  public:
    DelayModel(const Core &m) : module(m) {}

    sc_time irq_transfer(tlm_generic_payload &transaction) const {
      SC_LOG_DELAY(&module, "IRQ Transfer", module.irq_delay);
      return module.irq_delay;
    }
  };

  DelayModel delays{*this};

  // -------------------------------------------------------
  // Events
  // -------------------------------------------------------
  sc_event read_done;
  sc_event write_done;
  sc_event interrupt_request;

  // -------------------------------------------------------
  // Transport Functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_irq(int id, tlm_generic_payload &payload,
                                    tlm_phase &phase, sc_time &delay);

  tlm_sync_enum nb_transport_bw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);
};