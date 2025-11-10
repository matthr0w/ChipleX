#pragma once

#include <optional>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <yaml-cpp/yaml.h>

#include "logging.h"

#include "ARM/TLM/arm_axi4.h"
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
  simple_target_socket<Core> irq_socket;

  Core(sc_module_name name, unsigned chiplet_id, unsigned core_id,
       YAML::Node config, const CyclesDB &cycles);

  void core_thread();
  void interrupt_thread();

  std::function<void(Core &)> thread_fn;
  std::function<void(Core &, tlm_generic_payload *)> interrupt_fn;

  // -------------------------------------------------------
  // Program API
  // -------------------------------------------------------
  unsigned MAX_INCR_BURST_SIZE = 0;
  unsigned MAX_FIXED_BURST_SIZE = 0;
  unsigned MAX_WRAP_BURST_SIZE = 0;

  void wait_cycles(const std::string &name);

  // -------------------------------------------------------
  // AXI Request Types
  // -------------------------------------------------------
  struct AxiRequest {
    uint32_t request_id;
    unsigned char *data;
    unsigned data_length;

    std::optional<uint32_t> address;
    std::optional<std::string> src_module_name;
    std::optional<std::string> dst_chiplet_name;
    std::optional<std::string> dst_module_name;

    bool is_volatile = false;
    ARM::AXI::Burst burst = ARM::AXI::BURST_INCR;

    AxiRequest(uint32_t id, unsigned char *buf, unsigned len)
        : request_id(id), data(buf), data_length(len) {}

    AxiRequest &set_addr(uint32_t addr) {
      address = addr;
      return *this;
    }

    AxiRequest &to_module(const std::string &module_name) {
      src_module_name = module_name;
      return *this;
    }

    AxiRequest &to_target(const std::string &chiplet_name,
                          const std::string &module_name = "memory") {
      dst_chiplet_name = chiplet_name;
      dst_module_name = module_name;
      return *this;
    }

    AxiRequest &set_burst(ARM::AXI::Burst type) {
      burst = type;
      return *this;
    }

    AxiRequest &skip_cache(bool val = true) {
      is_volatile = val;
      return *this;
    }
  };

  struct AxiDMARequest {
    uint32_t request_id;
    unsigned data_length;

    std::optional<std::string> src_module_name;
    std::optional<std::string> dst_chiplet_name;
    std::optional<std::string> dst_module_name;
    std::optional<std::string> target_module_name;
    std::optional<uint32_t> request_addr;
    std::optional<uint32_t> target_addr;

    bool is_volatile = false;
    ARM::AXI::Burst burst = ARM::AXI::BURST_INCR;

    AxiDMARequest(uint32_t id, unsigned len)
        : request_id(id), data_length(len) {}

    AxiDMARequest &via(const std::string &module_name) {
      src_module_name = module_name;
      return *this;
    }

    AxiDMARequest &from(const std::string &chiplet_name,
                        const std::string &module_name,
                        const uint32_t address) {
      dst_chiplet_name = chiplet_name;
      dst_module_name = module_name;
      request_addr = address;
      return *this;
    }

    AxiDMARequest &to(const std::string &module_name, const uint32_t address) {
      target_module_name = module_name;
      target_addr = address;
      return *this;
    }

    AxiDMARequest &set_burst(ARM::AXI::Burst type) {
      burst = type;
      return *this;
    }

    AxiDMARequest &skip_cache(bool val = true) {
      is_volatile = val;
      return *this;
    }
  };

  struct RequestHandle {
    ARM::AXI::Payload *payload;
    unsigned char *data;

    bool completed = false;
    sc_time time_stamp;
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

  // -------------------------------------------------------
  // AXI API
  // -------------------------------------------------------
private:
  std::shared_ptr<RequestHandle> read_internal(
      uint32_t request_id, uint8_t src_module, uint8_t dst_chiplet,
      uint8_t dst_module, uint32_t address, bool fixed_address,
      unsigned char *data, unsigned data_length, ARM::AXI::Burst burst,
      bool is_volatile);
  std::shared_ptr<RequestHandle> write_internal(
      uint32_t request_id, uint8_t src_module, uint8_t dst_chiplet,
      uint8_t dst_module, uint32_t address, bool fixed_address,
      unsigned char *data, unsigned data_length, ARM::AXI::Burst burst,
      bool is_volatile);
  std::shared_ptr<RequestHandle> dma_internal(
      uint32_t request_id, uint8_t src_module, uint8_t dst_chiplet,
      uint8_t dst_module, uint8_t target_module, uint32_t request_addr,
      uint32_t target_addr, unsigned data_length, ARM::AXI::Burst burst,
      bool is_volatile);

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
  tlm_sync_enum nb_transport_fw_irq(tlm_generic_payload & payload,
                                    tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);
};