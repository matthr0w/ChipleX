#pragma once

#include <systemc>
#include <tlm>
#include <vector>

#include "ARM/TLM/arm_axi4.h"

#include "common/Tracker.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(Memory) {
public:
  sc_core::sc_in<bool> clock;

  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  ARM::AXI::SimpleTargetSocket<Memory> tsocket;

  Memory(sc_module_name name, unsigned int axi_width, unsigned int size);

private:
  enum ChannelState { CLEAR, REQ, ACK };

  ChannelState r_state = CLEAR;
  ChannelState b_state = CLEAR;

  std::deque<ARM::AXI::Payload *> ar_queue;
  std::deque<ARM::AXI::Payload *> aw_queue;
  std::deque<ARM::AXI::Payload *> w_queue;

  ARM::AXI::Payload *r_outgoing = nullptr;
  ARM::AXI::Payload *b_outgoing = nullptr;

  unsigned r_beat_count;

  // memory
  std::vector<uint8_t> mem;
  std::vector<bool> write_flags;
  std::map<uint32_t, unsigned> allocated_ranges;
  uint32_t offchip_base_address;

  struct FlitKey {
    int request_id;
    int source_id;
    int core_id;

    bool operator==(const FlitKey &other) const {
      return request_id == other.request_id && source_id == other.source_id &&
             core_id == other.core_id;
    }
  };

  struct FlitKeyHash {
    std::size_t operator()(const FlitKey &key) const {
      std::size_t h1 = std::hash<int>()(key.request_id);
      std::size_t h2 = std::hash<int>()(key.source_id);
      std::size_t h3 = std::hash<int>()(key.core_id);
      return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
  };

  std::unordered_map<FlitKey, uint32_t, FlitKeyHash> pending_flit_writes;
  unsigned flit_data_size = 0;
  unsigned flit_id = 0;

  void clock_posedge();
  void clock_negedge();

  // -------------------------------------------------------
  // state variables
  // -------------------------------------------------------
  bool active_txn = false;
  uint32_t active_addr = UINT32_MAX;

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int size;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);

  // -------------------------------------------------------
  // helper functions
  // -------------------------------------------------------
  void set_active_address(ARM::AXI::Payload & payload);
  uint32_t set_flit_address(ARM::AXI::Payload & payload);
  uint32_t set_beat_address(uint32_t base, unsigned beat_idx,
                            unsigned beat_bytes, unsigned beats,
                            ARM::AXI::Burst burst);

  uint32_t allocate_dynamic_address(bool onchip, unsigned size);
  void deallocate_dynamic_address(uint32_t address, unsigned size);

  // -------------------------------------------------------
  // debug functions
  // -------------------------------------------------------
public:
  void report_usage();
};