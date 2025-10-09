#pragma once

#include <optional>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <yaml-cpp/yaml.h>

#include "logging.h"

#include "ARM/TLM/arm_axi4.h"

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
  simple_target_socket<Core> irq_socket;

  Core(sc_module_name name, unsigned chiplet_id, unsigned core_id,
       YAML::Node config);

  void core_thread();
  void interrupt_thread();

  std::function<void(Core &)> thread_fn;
  std::function<void(Core &, tlm_generic_payload *)> interrupt_fn;

  void wait_cycles(unsigned count);

  // -------------------------------------------------------
  // Usercode Types
  // -------------------------------------------------------
  struct ReadRequest {
    uint32_t request_id;
    uint32_t address;
    unsigned char *data;
    unsigned data_length;

    std::optional<uint8_t> destination_id;
    bool is_volatile = false;

    ARM::AXI::Burst burst = ARM::AXI::BURST_INCR;

    ReadRequest(uint32_t id, uint32_t addr, unsigned char *buf, unsigned len)
        : request_id(id), address(addr), data(buf), data_length(len) {}

    ReadRequest &set_dest(uint8_t dest) {
      destination_id = dest;
      return *this;
    }
    ReadRequest &set_burst(ARM::AXI::Burst type) {
      burst = type;
      return *this;
    }
    ReadRequest &skip_cache(bool val = true) {
      is_volatile = val;
      return *this;
    }
  };

  struct WriteRequest {
    uint32_t request_id;
    unsigned char *data;
    unsigned data_length;

    std::optional<uint8_t> destination_id;
    std::optional<uint32_t> address;
    bool is_volatile = false;

    ARM::AXI::Burst burst = ARM::AXI::BURST_INCR;

    WriteRequest(uint32_t id, unsigned char *buf, unsigned len)
        : request_id(id), data(buf), data_length(len) {}

    WriteRequest &set_dest(uint8_t dest) {
      destination_id = dest;
      return *this;
    }
    WriteRequest &set_addr(uint32_t addr) {
      address = addr;
      return *this;
    }
    WriteRequest &set_burst(ARM::AXI::Burst type) {
      burst = type;
      return *this;
    }
    WriteRequest &skip_cache(bool val = true) {
      is_volatile = val;
      return *this;
    }
  };

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
      if (!completed)
        ::wait(done);
    }
  };

  unsigned MAX_INCR_BURST_SIZE = 0;
  unsigned MAX_FIXED_BURST_SIZE = 0;
  unsigned MAX_WRAP_BURST_SIZE = 0;

private:
  // -------------------------------------------------------
  // Internal Declarations
  // -------------------------------------------------------
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
  tlm_sync_enum nb_transport_fw_irq(tlm_generic_payload & payload,
                                    tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);

  // -------------------------------------------------------
  // AXI API
  // -------------------------------------------------------
private:
  RequestHandle *read_internal(uint32_t request_id, uint8_t destination_id,
                               uint32_t address, bool fixed_address,
                               unsigned char *data, unsigned data_length,
                               ARM::AXI::Burst burst, bool is_volatile);

  RequestHandle *write_internal(uint32_t request_id, uint8_t destination_id,
                                uint32_t address, bool fixed_address,
                                unsigned char *data, unsigned data_length,
                                ARM::AXI::Burst burst, bool is_volatile);

public:
  RequestHandle *read(const ReadRequest &req);

  RequestHandle *write(const WriteRequest &req);
};