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
  sc_core::sc_in<bool> clock;

  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  ARM::AXI::SimpleInitiatorSocket<Core> isocket;
  // simple_target_socket<Core> irq_socket;

  Core(sc_module_name name, unsigned int chip_id, unsigned int core_id,
       unsigned int axi_width, sc_time irq_delay);

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

    ARM::AXI::Payload *get_payload() const { return payload; }
  };

  unsigned MAX_INCR_BURST_SIZE = 0;
  unsigned MAX_FIXED_BURST_SIZE = 0;
  unsigned MAX_WRAP_BURST_SIZE = 0;

  void core_thread();
  void interrupt_thread();

private:
  enum ChannelState { CLEAR, REQ, ACK };

  ChannelState ar_state = CLEAR;
  ChannelState aw_state = CLEAR;
  ChannelState w_state = CLEAR;

  std::deque<ARM::AXI::Payload *> ar_queue;
  std::deque<ARM::AXI::Payload *> aw_queue;
  std::deque<ARM::AXI::Payload *> w_queue;

  unsigned w_beat_count = 0;

  std::unordered_map<ARM::AXI::Payload *, RequestHandle *> request_handles;

  std::deque<tlm_generic_payload *> irq_queue;

  void clock_posedge();
  void clock_negedge();

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int chip_id;
  const unsigned int core_id;
  const unsigned int axi_width;
  const sc_time irq_delay;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event read_done;
  sc_event write_done;
  sc_event interrupt_request;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_irq(tlm_generic_payload & payload,
                                    tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);

  // -------------------------------------------------------
  // delay model
  // -------------------------------------------------------
  struct DelayModel {
  private:
    const Core &module;

  public:
    DelayModel(const Core &m) : module(m) {}

    sc_time irq_transfer(tlm_generic_payload &transaction) const {
      SC_LOG_DELAY(&module, transaction, "IRQ Transfer", module.irq_delay);
      return module.irq_delay;
    }
  };

  DelayModel delays{*this};

  // -------------------------------------------------------
  // AXI methods
  // -------------------------------------------------------
private:
  RequestHandle *read_internal(int request_id, int destination_id,
                               uint32_t address, bool fixed_address,
                               unsigned char *data, unsigned int data_length,
                               ARM::AXI::Burst burst, bool is_volatile);

  RequestHandle *write_internal(int request_id, int destination_id,
                                uint32_t address, bool fixed_address,
                                unsigned char *data, unsigned int data_length,
                                ARM::AXI::Burst burst, bool is_volatile);

public:
  RequestHandle *read(int request_id, int destination_id, uint32_t address,
                      unsigned char *data, unsigned int data_length,
                      bool is_volatile);

  RequestHandle *read_fixed(int request_id, int destination_id,
                            uint32_t address, unsigned char *data,
                            unsigned int data_length, bool is_volatile);

  RequestHandle *read_wrap(int request_id, int destination_id, uint32_t address,
                           unsigned char *data, unsigned int data_length,
                           bool is_volatile);

  RequestHandle *write(int request_id, int destination_id, uint32_t address,
                       unsigned char *data, unsigned int data_length,
                       bool is_volatile);

  RequestHandle *write(int request_id, int destination_id, unsigned char *data,
                       unsigned int data_length);

  RequestHandle *write_fixed(int request_id, int destination_id,
                             uint32_t address, unsigned char *data,
                             unsigned int data_length, bool is_volatile);

  RequestHandle *write_fixed(int request_id, int destination_id,
                             unsigned char *data, unsigned int data_length);

  RequestHandle *write_wrap(int request_id, int destination_id,
                            uint32_t address, unsigned char *data,
                            unsigned int data_length, bool is_volatile);

  RequestHandle *write_wrap(int request_id, int destination_id,
                            unsigned char *data, unsigned int data_length);
};