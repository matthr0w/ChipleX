#pragma once

#include <memory>
#include <systemc>
#include <tlm>

#include "ARM/TLM/arm_axi4.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(AXIBus) {
public:
  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  std::vector<std::unique_ptr<ARM::AXI4::SimpleTargetSocketTagged<AXIBus>>>
      mgr_tsockets;
  std::vector<std::unique_ptr<ARM::AXI4::SimpleInitiatorSocketTagged<AXIBus>>>
      sub_isockets;

  AXIBus(sc_module_name name, unsigned int chip_id, unsigned int axi_width,
         unsigned int num_managers, unsigned int num_subordinates);
  ~AXIBus();

private:
  struct ChannelState {
    bool locked = false;
    ARM::AXI::Payload *cur =
        nullptr;  // Transaction currently using this channel
    int mgr = -1; // Manager ID
  };

  struct SubState {
    ChannelState W; // AW/W/B channel state
    ChannelState R; // AR/R channel state
  };

  std::vector<SubState> sub_state;

  std::unordered_map<ARM::AXI::Payload *, int> payloads2mgr;
  std::unordered_map<ARM::AXI::Payload *, int> payloads2sub;

  sc_mutex fw_mutex;
  sc_mutex bw_mutex;

  // Monitor
  uint8_t *beat_data;
  std::unordered_map<ARM::AXI::Payload *, ARM::AXI::Phase> payload_phase_map;
  std::unordered_map<ARM::AXI::Payload *, unsigned> payload_burst_index;

  // -------------------------------------------------------
  // Parameters
  // -------------------------------------------------------
  const unsigned int chip_id;
  const unsigned int axi_width;

  // -------------------------------------------------------
  // Transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(int mgr_id, ARM::AXI::Payload &payload,
                                ARM::AXI::Phase &phase);

  tlm_sync_enum nb_transport_bw(int sub_id, ARM::AXI::Payload &payload,
                                ARM::AXI::Phase &phase);

  // -------------------------------------------------------
  // Helper functions
  // -------------------------------------------------------
  int route_payload(ARM::AXI::Payload & payload);

  // -------------------------------------------------------
  // Debug functions
  // -------------------------------------------------------
  void print_payload(ARM::AXI::Payload & payload, ARM::AXI::Phase sent_phase,
                     tlm_sync_enum reply, ARM::AXI::Phase reply_phase);
};