#pragma once

#include <memory>
#include <systemc>
#include <tlm>

#include "include/ARM/TLM/arm_axi4.h"

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

  AXIBus(sc_module_name name, unsigned int chip_id, unsigned int num_managers,
         unsigned int num_subordinates);

private:
  struct ChanState {
    bool busy = false;
    ARM::AXI::Payload *cur =
        nullptr;                  // transaction currently using this channel
    int mgr = -1;                 // source manager ID
    bool completion_mark = false; // set on R_VALID_LAST / B_VALID
  };
  struct SubState {
    ChanState R; // AR/R channel state
    ChanState W; // AW/W/B channel state
  };
  std::vector<SubState> sub_state;

  std::unordered_map<ARM::AXI::Payload *, int> payloads2mgr;
  std::unordered_map<ARM::AXI::Payload *, int> payloads2sub;

  sc_mutex fw_mutex;
  sc_mutex bw_mutex;

  int route(ARM::AXI::Payload & payload) { return 0; } // fixed for now

  bool is_ar_valid(ARM::AXI::Phase & phase) {
    return phase == ARM::AXI::AR_VALID;
  }
  bool is_r_ready(ARM::AXI::Phase & phase) {
    return phase == ARM::AXI::R_READY;
  }
  bool is_r_valid_last(ARM::AXI::Phase & phase) {
    return phase == ARM::AXI::R_VALID_LAST;
  }
  bool is_aw_valid(ARM::AXI::Phase & phase) {
    return phase == ARM::AXI::AW_VALID;
  }
  bool is_w_valid(ARM::AXI::Phase & phase) {
    return phase == ARM::AXI::W_VALID;
  }
  bool is_w_valid_last(ARM::AXI::Phase & phase) {
    return phase == ARM::AXI::W_VALID_LAST;
  }
  bool is_b_ready(ARM::AXI::Phase & phase) {
    return phase == ARM::AXI::B_READY;
  }
  bool is_b_valid(ARM::AXI::Phase & phase) {
    return phase == ARM::AXI::B_VALID;
  }

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int chip_id;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(int mgr_id, ARM::AXI::Payload &payload,
                                ARM::AXI::Phase &phase);

  tlm_sync_enum nb_transport_bw(int sub_id, ARM::AXI::Payload &payload,
                                ARM::AXI::Phase &phase);
};