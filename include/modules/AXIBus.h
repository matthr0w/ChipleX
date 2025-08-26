#pragma once

#include <map>
#include <memory>
#include <systemc>
#include <tlm>

#include "ARM/TLM/arm_axi4.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(AXIBus) {
public:
  // -------------------------------------------------------
  // sockets
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
    bool busy = false;
    ARM::AXI::Payload *cur =
        nullptr;  // transaction currently using this channel
    int mgr = -1; // manager id
  };

  struct SubState {
    ChannelState R; // AR/R channel state
    ChannelState W; // AW/W/B channel state
  };

  std::vector<SubState> sub_state;

  std::unordered_map<ARM::AXI::Payload *, int> payloads2mgr;
  std::unordered_map<ARM::AXI::Payload *, int> payloads2sub;

  sc_mutex fw_mutex;
  sc_mutex bw_mutex;

  // debug output
  uint8_t *beat_data;
  std::map<ARM::AXI::Payload *, unsigned> payload_burst_index;

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int chip_id;
  const unsigned int axi_width;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(int mgr_id, ARM::AXI::Payload &payload,
                                ARM::AXI::Phase &phase);

  tlm_sync_enum nb_transport_bw(int sub_id, ARM::AXI::Payload &payload,
                                ARM::AXI::Phase &phase);

  // -------------------------------------------------------
  // helper functions
  // -------------------------------------------------------
  int route_payload(ARM::AXI::Payload & payload) { return 0; } // fixed for now

  // -------------------------------------------------------
  // debug functions
  // -------------------------------------------------------
  void print_payload(ARM::AXI::Payload & payload, ARM::AXI::Phase sent_phase,
                     tlm_sync_enum reply, ARM::AXI::Phase reply_phase);
};