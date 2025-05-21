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
  const Config &chiplet_config = ConfigRegistry::instance().get("Chiplet");

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