#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <unordered_map>
#include <vector>

#include "ARM/TLM/arm_axi4.h"

using namespace sc_core;
using namespace tlm;

struct VirtualAXIInitiatorIF {
  virtual tlm::tlm_sync_enum nb_transport_bw_axi(ARM::AXI::Payload &payload,
                                                 ARM::AXI::Phase &phase) = 0;
  virtual ~VirtualAXIInitiatorIF() = default;
};

SC_MODULE(DMAEngine) {
public:
  sc_in<bool> clock;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  ARM::AXI::SimpleInitiatorSocket<DMAEngine> isocket;

  DMAEngine(sc_module_name name, unsigned int axi_width);

  int register_virtual_initiator(VirtualAXIInitiatorIF * owner);
  void unregister_virtual_initiator(int vm_id);

  bool forward_from_virtual(int vm_id, ARM::AXI::Payload &payload);

private:
  enum ChannelState { CLEAR, REQ, ACK };

  ChannelState ar_state = CLEAR;
  ChannelState aw_state = CLEAR;
  ChannelState w_state = CLEAR;

  std::deque<ARM::AXI::Payload *> ar_queue;
  std::deque<ARM::AXI::Payload *> aw_queue;
  std::deque<ARM::AXI::Payload *> w_queue;

  unsigned w_beat_count = 0;

  std::vector<VirtualAXIInitiatorIF *> owners_;
  std::unordered_map<ARM::AXI::Payload *, int> payload_owner_map_;

  void clock_posedge();
  void clock_negedge();

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int axi_width;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_bw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);
};