#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "ARM/TLM/arm_axi4.h"
#include "logging.h"

#include "common/Tracker.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Core) {
public:
  sc_core::sc_in<bool> clk;

  // -------------------------------------------------------
  // Trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  ARM::AXI::SimpleInitiatorSocket<Core> isocket;
  simple_target_socket<Core> irq_socket;

  Core(sc_module_name name, unsigned chip_id, unsigned core_id,
       unsigned axi_width, sc_time irq_delay);

  std::function<void(Core &, UtilizationTracker *)> thread_fn;
  std::function<void(Core &, UtilizationTracker *, tlm_generic_payload *)>
      interrupt_fn;

  struct RequestHandle {
    ARM::AXI::Payload *payload;
    unsigned char *data;

    bool completed = false;
    sc_event done;

    RequestHandle() : payload(nullptr) {}

    void notify(sc_time delay) {
      completed = true;
      if (payload->get_command() == ARM::AXI::COMMAND_READ)
        payload->read_out(data);
      done.notify(delay);
    }

    void wait() {
      if (!completed) {
        ::wait(done);
      }
    }
  };

  unsigned MAX_INCR_BURST_SIZE = 0;
  unsigned MAX_FIXED_BURST_SIZE = 0;
  unsigned MAX_WRAP_BURST_SIZE = 0;

  void core_thread();
  void interrupt_thread();

private:
  enum ChannelState { CLEAR, REQ, ACK };

  ChannelState aw_state = CLEAR;
  ChannelState w_state = CLEAR;
  ChannelState ar_state = CLEAR;

  std::deque<ARM::AXI::Payload *> aw_queue;
  std::deque<ARM::AXI::Payload *> w_queue;
  std::deque<ARM::AXI::Payload *> ar_queue;

  unsigned w_beat_count = 0;

  std::unordered_map<ARM::AXI::Payload *, RequestHandle *> request_handles;

  std::deque<tlm_generic_payload *> irq_queue;

  void clk_posedge();
  void clk_negedge();

  // -------------------------------------------------------
  // Parameters
  // -------------------------------------------------------
  const unsigned chip_id;
  const unsigned core_id;
  const unsigned axi_width;
  const sc_time irq_delay;

  // -------------------------------------------------------
  // Delay model
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
  // Transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_irq(tlm_generic_payload & payload,
                                    tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);

  // -------------------------------------------------------
  // AXI methods
  // -------------------------------------------------------
private:
  RequestHandle *read_internal(uint32_t request_id, int destination_id,
                               uint32_t address, bool fixed_address,
                               unsigned char *data, unsigned data_length,
                               ARM::AXI::Burst burst, bool is_volatile);

  RequestHandle *write_internal(uint32_t request_id, int destination_id,
                                uint32_t address, bool fixed_address,
                                unsigned char *data, unsigned data_length,
                                ARM::AXI::Burst burst, bool is_volatile);

public:
  RequestHandle *read(uint32_t request_id, int destination_id, uint32_t address,
                      unsigned char *data, unsigned data_length,
                      bool is_volatile);

  RequestHandle *read_fixed(uint32_t request_id, int destination_id,
                            uint32_t address, unsigned char *data,
                            unsigned data_length, bool is_volatile);

  RequestHandle *read_wrap(uint32_t request_id, int destination_id,
                           uint32_t address, unsigned char *data,
                           unsigned data_length, bool is_volatile);

  RequestHandle *write(uint32_t request_id, int destination_id,
                       uint32_t address, unsigned char *data,
                       unsigned data_length, bool is_volatile);

  RequestHandle *write(uint32_t request_id, int destination_id,
                       unsigned char *data, unsigned data_length);

  RequestHandle *write_fixed(uint32_t request_id, int destination_id,
                             uint32_t address, unsigned char *data,
                             unsigned data_length, bool is_volatile);

  RequestHandle *write_fixed(uint32_t request_id, int destination_id,
                             unsigned char *data, unsigned data_length);

  RequestHandle *write_wrap(uint32_t request_id, int destination_id,
                            uint32_t address, unsigned char *data,
                            unsigned data_length, bool is_volatile);

  RequestHandle *write_wrap(uint32_t request_id, int destination_id,
                            unsigned char *data, unsigned data_length);
};