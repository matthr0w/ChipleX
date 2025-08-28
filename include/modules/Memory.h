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

  Memory(sc_module_name name, unsigned int size, unsigned int axi_width);

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

  // fsm
  bool active_txn = false;
  uint32_t active_addr = UINT32_MAX;

  // memory
  std::vector<uint8_t> mem;
  std::vector<bool> write_flags;
  std::map<uint32_t, unsigned int> allocated_ranges;
  uint32_t offchip_base_address;

  void clock_posedge();
  void clock_negedge();

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
  uint32_t set_beat_address(uint64_t base, unsigned beat_idx,
                            unsigned beat_bytes, unsigned beats,
                            ARM::AXI::Burst burst);

  uint32_t allocate_dynamic_address(ARM::AXI::Payload & payload, bool onchip,
                                    uint32_t size);
  void deallocate_dynamic_address(ARM::AXI::Payload & payload, uint32_t address,
                                  unsigned int size);

  // -------------------------------------------------------
  // debug functions
  // -------------------------------------------------------
public:
  void report_usage();
};