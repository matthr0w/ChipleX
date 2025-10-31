#pragma once

#include <memory>
#include <systemc>
#include <tlm>
#include <yaml-cpp/yaml.h>

#include "ARM/TLM/arm_axi4.h"
#include "common/Statistics.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(AXIBus) {
private:
  // -------------------------------------------------------
  // Config
  // -------------------------------------------------------
  const unsigned chiplet_id;
  const unsigned axi_width;
  const sc_time clk_cycle;

public:
  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  std::vector<std::unique_ptr<ARM::AXI4::SimpleTargetSocketTagged<AXIBus>>>
      mgr_tsockets;
  std::vector<std::unique_ptr<ARM::AXI4::SimpleInitiatorSocketTagged<AXIBus>>>
      sub_isockets;

  AXIBus(sc_module_name name, unsigned chiplet_id, unsigned num_managers,
         unsigned num_subordinates, YAML::Node config);
  ~AXIBus();

private:
  // -------------------------------------------------------
  // Internal Declarations
  // -------------------------------------------------------
  StatManager &stats = StatManager::instance();

  std::unordered_map<ARM::AXI::Payload *, int> payloads2mgr;
  std::unordered_map<ARM::AXI::Payload *, int> payloads2sub;

  sc_mutex fw_mutex;
  sc_mutex bw_mutex;

  // Monitor
  uint8_t *beat_data;
  std::unordered_map<ARM::AXI::Payload *, ARM::AXI::Phase> payload_phase_map;
  std::unordered_map<ARM::AXI::Payload *, unsigned> payload_burst_index;

  // -------------------------------------------------------
  // Transport Functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(int mgr_id, ARM::AXI::Payload &payload,
                                ARM::AXI::Phase &phase);

  tlm_sync_enum nb_transport_bw(int sub_id, ARM::AXI::Payload &payload,
                                ARM::AXI::Phase &phase);

  // -------------------------------------------------------
  // Helper Functions
  // -------------------------------------------------------
  int route_payload(ARM::AXI::Payload & payload);

  // -------------------------------------------------------
  // Debug Functions
  // -------------------------------------------------------
  void print_payload(ARM::AXI::Payload & payload, ARM::AXI::Phase sent_phase,
                     tlm_sync_enum reply, ARM::AXI::Phase reply_phase);
};