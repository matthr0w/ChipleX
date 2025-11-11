#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <yaml-cpp/yaml.h>

#include "ARM/TLM/arm_axi4.h"
#include "common/Statistics.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

struct DMARequest {
  uint32_t request_id; // Request ID
  uint8_t core_id;     // Core ID

  uint8_t burst;        // Burst type
  uint8_t is_volatile;  // Cache usage
  unsigned data_length; // Bytes to transfer

  // Read data from
  uint8_t src_chiplet;   // Chiplet ID of request chiplet
  uint8_t src_module;    // Module ID on request chiplet
  uint8_t dst_chiplet;   // Chiplet ID of target chiplet
  uint8_t dst_module;    // Module ID on target chiplet
  uint32_t request_addr; // Address to read from

  // Write data to
  uint8_t target_module; // Module ID on request chiplet
  uint32_t target_addr;  // Address to write to
} __attribute__((packed));
static_assert(sizeof(DMARequest) % 8 == 0);

struct DMAForwardInterface {
  virtual tlm_sync_enum nb_transport_bw_axi(ARM::AXI::Payload &payload,
                                            ARM::AXI::Phase &phase) = 0;
  virtual ~DMAForwardInterface() = default;
};

SC_MODULE(DMAEngine) {
private:
  // -------------------------------------------------------
  // Config
  // -------------------------------------------------------
  const unsigned num_cores;
  const unsigned axi_width;
  const sc_time clk_cycle;

public:
  // -------------------------------------------------------
  // Signals
  // -------------------------------------------------------
  sc_in<bool> clk;

  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  ARM::AXI::SimpleTargetSocket<DMAEngine> tsocket;
  ARM::AXI::SimpleInitiatorSocket<DMAEngine> isocket;

  simple_initiator_socket_tagged<DMAEngine> *irq_sockets;

  DMAEngine(sc_module_name name, YAML::Node config);
  ~DMAEngine();

private:
  // -------------------------------------------------------
  // Internal Declarations
  // -------------------------------------------------------
  StatManager &stats = StatManager::instance();

  enum ChannelState { CLEAR, REQ, ACK };

  ChannelState aw_state = CLEAR;
  ChannelState w_state = CLEAR;
  ChannelState b_state = CLEAR;
  ChannelState ar_state = CLEAR;

  std::deque<ARM::AXI::Payload *> aw_queue_in;
  std::deque<ARM::AXI::Payload *> aw_queue_out;
  std::deque<ARM::AXI::Payload *> w_queue_in;
  std::deque<ARM::AXI::Payload *> w_queue_out;
  std::deque<ARM::AXI::Payload *> ar_queue_out;

  ARM::AXI::Payload *b_outgoing = nullptr;

  unsigned w_beat_count = 0;
  unsigned r_beat_count = 0;

  std::vector<DMAForwardInterface *> owners;
  std::unordered_map<ARM::AXI::Payload *, int> payload_owner_map;

  int internal_vm_id = -1;
  DMARequest current_request;
  ARM::AXI::Payload *fetch_write_payload = nullptr;

  enum class DMAEngineState {
    Idle,
    WriteForward,
    ReadForward,
    ReadFetch,
    WriteFetch
  };
  DMAEngineState state = DMAEngineState::Idle;

  void clk_posedge();
  void clk_negedge();

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
  ARM::AXI::Payload *issue_fetch_read(const DMARequest &req);
  ARM::AXI::Payload *issue_fetch_write(const DMARequest &req);

  void send_irq(ARM::AXI::Payload & payload, unsigned core_id);

public:
  // -------------------------------------------------------
  // API
  // -------------------------------------------------------
  int register_virtual_initiator(DMAForwardInterface * owner);
  void unregister_virtual_initiator(int vm_id);

  bool forward_from_virtual(int vm_id, ARM::AXI::Payload &payload,
                            ARM::AXI::Channel channel);
};