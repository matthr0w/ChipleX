#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <vector>

#include "common/Tracker.h"

#include "include/configs.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

namespace fpga {
SC_MODULE(RAM) {
public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<RAM> socket;

  SC_CTOR(RAM);

  void report_usage();

private:
  // -------------------------------------------------------
  // config
  // -------------------------------------------------------
  const Config &fpga_config = ConfigRegistry::instance().get("FPGA");

  const unsigned int ram_size = fpga_config.get<unsigned int>("ram.size");
  const unsigned int ram_width = fpga_config.get<unsigned int>("ram.width");
  const sc_time ram_clk_cycle = fpga_config.get<sc_time>("ram.clk_cycle");
  const sc_time ram_address_delay =
      fpga_config.get<sc_time>("ram.address_delay");
  const sc_time ram_access_delay = fpga_config.get<sc_time>("ram.access_delay");

  std::vector<uint8_t> mem;
  std::vector<bool> written_flags;

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm_generic_payload> peq;
  void process_transaction();

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);
};
}; // namespace fpga