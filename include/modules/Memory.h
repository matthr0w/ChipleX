#pragma once

#include <map>
#include <systemc>
#include <tlm>
#include <vector>
#include <yaml-cpp/yaml.h>

#include "ARM/TLM/arm_axi4.h"
#include "common/Statistics.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(Memory) {
private:
  // -------------------------------------------------------
  // Config
  // -------------------------------------------------------
  const unsigned axi_width;
  const unsigned size;

public:
  // -------------------------------------------------------
  // Signals
  // -------------------------------------------------------
  sc_in<bool> clk;

  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  ARM::AXI::SimpleTargetSocket<Memory> tsocket;

  Memory(sc_module_name name, YAML::Node config);

private:
  // -------------------------------------------------------
  // Internal Declarations
  // -------------------------------------------------------
  StatisticsManager &stats = StatisticsManager::instance();

  enum ChannelState { CLEAR, REQ, ACK };

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

  std::vector<uint8_t> mem;
  MemoryState mem_state = MemoryState::Idle;
  std::vector<bool> write_flags;
  std::map<uint32_t, unsigned> allocated_ranges;
  uint32_t offchip_base_address;
  uint32_t active_addr = 0;

  void clk_posedge();
  void clk_negedge();

  // -------------------------------------------------------
  // Transport Functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);

  // -------------------------------------------------------
  // Helper Functions
  // -------------------------------------------------------
  void set_active_address(ARM::AXI::Payload & payload);

  uint32_t set_beat_address(uint32_t base, unsigned beat_idx,
                            unsigned beat_bytes, unsigned beats,
                            ARM::AXI::Burst burst);

  uint32_t allocate_dynamic_address(bool onchip, unsigned size);
  void deallocate_dynamic_address(uint32_t address, unsigned size);
};