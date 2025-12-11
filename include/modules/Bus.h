#pragma once

#include <memory>
#include <systemc>
#include <tlm>

#include "ARM/TLM/arm_axi4.h"
#include "common/Statistics.h"
#include "setup/Types.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(Bus) {
private:
  // -------------------------------------------------------
  // Config
  // -------------------------------------------------------
  const unsigned chiplet_id;
  const unsigned axi_width;
  const sc_time clk_cycle;

public:
  // -------------------------------------------------------
  // Signals
  // -------------------------------------------------------
  sc_in<bool> clk;

  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  std::vector<std::unique_ptr<ARM::AXI4::SimpleTargetSocketTagged<Bus>>>
      managers;
  std::vector<std::unique_ptr<ARM::AXI4::SimpleInitiatorSocketTagged<Bus>>>
      subordinates;

  Bus(sc_module_name name, unsigned chiplet_id, ChipletConfig chiplet_config,
      unsigned num_managers, unsigned num_subordinates);
  ~Bus();

private:
  // -------------------------------------------------------
  // Internal Declarations
  // -------------------------------------------------------
  StatManager &stats = StatManager::instance();

  struct Request {
    ARM::AXI::Payload *payload;
    ARM::AXI::Phase phase;
  };

  struct Connection {
    std::deque<Request> fw_q;
    std::deque<Request> bw_q;
    std::deque<Request> next_fw_q;
    std::deque<Request> next_bw_q;
  };

  std::map<std::pair<int, int>, Connection> connections;

  std::unordered_map<ARM::AXI::Payload *, int> payloads2mgr;
  std::unordered_map<ARM::AXI::Payload *, int> payloads2sub;

  std::map<unsigned, std::string> mgr_names;
  std::map<unsigned, std::string> sub_names;
  std::map<unsigned, std::string> interconnect_names;

  // Monitor
  uint8_t *beat_data;
  std::unordered_map<ARM::AXI::Payload *, ARM::AXI::Phase> payload_phase_map;
  std::unordered_map<ARM::AXI::Payload *, unsigned> payload_burst_index;

  void clk_posedge();

  // -------------------------------------------------------
  // Transport Functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(int mgr_id, ARM::AXI::Payload &payload,
                                ARM::AXI::Phase &phase);

  tlm_sync_enum nb_transport_bw(int sub_id, ARM::AXI::Payload &payload,
                                ARM::AXI::Phase &phase);

  // -------------------------------------------------------
  // Debug Functions
  // -------------------------------------------------------
  void print_payload(ARM::AXI::Payload & payload, ARM::AXI::Phase sent_phase,
                     ARM::AXI::Phase reply_phase);
};