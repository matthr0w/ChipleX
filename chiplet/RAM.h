#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <vector>

#include "include/configs.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

namespace chiplet {
SC_MODULE(RAM) {
public:
  simple_target_socket<RAM> socket;

  SC_CTOR(RAM);

private:
  // -------------------------------------------------------
  // config
  // -------------------------------------------------------
  const Config &chiplet_config = ConfigRegistry::instance().get("Chiplet");

  const unsigned int bus_width = chiplet_config.get<unsigned int>("bus.width");
  const sc_time bus_clk_cycle = chiplet_config.get<sc_time>("bus.clk_cycle");
  const unsigned int ram_size = chiplet_config.get<unsigned int>("ram.size");
  const unsigned int ram_width = chiplet_config.get<unsigned int>("ram.width");
  const sc_time ram_clk_cycle = chiplet_config.get<sc_time>("ram.clk_cycle");
  const sc_time ram_access_delay =
      chiplet_config.get<sc_time>("ram.access_delay");

  std::vector<uint8_t> mem;

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
}; // namespace chiplet