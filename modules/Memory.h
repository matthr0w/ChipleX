#pragma once

#include <systemc>
#include <tlm>
#include <vector>

#include "common/Tracker.h"

#include "include/ARM/TLM/arm_axi4.h"
#include "include/logging.h"

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

  Memory(sc_module_name name, unsigned int size);

  void report_usage();

private:
  enum ChannelState { CLEAR, REQ, ACK };

  ChannelState b_state;
  ChannelState r_state;

  std::deque<ARM::AXI::Payload *> aw_queue;
  std::deque<ARM::AXI::Payload *> w_queue;
  std::deque<ARM::AXI::Payload *> ar_queue;

  ARM::AXI::Payload *b_outgoing;
  ARM::AXI::Payload *r_outgoing;

  unsigned r_beat_count;

  bool addr_phase_pending = false;
  bool active_txn = false;

  std::vector<uint8_t> mem;
  std::vector<bool> write_flags;
  uint32_t offchip_base_address;
  std::map<uint32_t, unsigned int> allocated_ranges;

  void clock_posedge();
  void clock_negedge();

  void set_address(ARM::AXI::Payload & payload);
  uint32_t allocate_dynamic_address(ARM::AXI::Payload & payload, bool onchip,
                                    uint32_t size);
  void deallocate_dynamic_address(ARM::AXI::Payload & payload, uint32_t address,
                                  unsigned int size);

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int size;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);
};