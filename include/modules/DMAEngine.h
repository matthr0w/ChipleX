#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <yaml-cpp/yaml.h>

#include "ARM/TLM/arm_axi4.h"

using namespace sc_core;
using namespace tlm;

struct VirtualAXIInitiatorIF {
  virtual tlm_sync_enum nb_transport_bw_axi(ARM::AXI::Payload &payload,
                                            ARM::AXI::Phase &phase) = 0;
  virtual ~VirtualAXIInitiatorIF() = default;
};

SC_MODULE(DMAEngine) {
private:
  // -------------------------------------------------------
  // Config
  // -------------------------------------------------------
  const unsigned axi_width;

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

  DMAEngine(sc_module_name name, YAML::Node config);

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

  std::vector<VirtualAXIInitiatorIF *> owners;
  std::unordered_map<ARM::AXI::Payload *, int> payload_owner_map;

  void clk_posedge();
  void clk_negedge();

  // -------------------------------------------------------
  // Transport Functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_bw(ARM::AXI::Payload & payload,
                                ARM::AXI::Phase & phase);

public:
  // -------------------------------------------------------
  // API
  // -------------------------------------------------------
  int register_virtual_initiator(VirtualAXIInitiatorIF * owner);
  void unregister_virtual_initiator(int vm_id);

  bool forward_from_virtual(int vm_id, ARM::AXI::Payload &payload,
                            ARM::AXI::Channel channel);
};