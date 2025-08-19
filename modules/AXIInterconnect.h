#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(AXIInterconnect) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket_tagged<AXIInterconnect> *tsockets;
  simple_initiator_socket_tagged<AXIInterconnect> *isockets;

  AXIInterconnect(sc_module_name name, unsigned int chip_id,
                  unsigned int num_managers, unsigned int num_subordinates,
                  sc_time axi_clk_cycle, sc_time axi_arbitration_delay);

private:
  sc_mutex request_mutex;
  std::unordered_map<tlm::tlm_generic_payload *, int> requests_map;

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int chip_id;
  const sc_time axi_clk_cycle;
  const sc_time axi_arbitration_delay;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(int id, tlm_generic_payload &transaction,
                                tlm_phase &phase, sc_time &delay);

  tlm_sync_enum nb_transport_bw(int id, tlm_generic_payload &transaction,
                                tlm_phase &phase, sc_time &delay);

  // -------------------------------------------------------
  // delay model
  // -------------------------------------------------------
  struct DelayModel {
  private:
    const AXIInterconnect &module;

  public:
    DelayModel(const AXIInterconnect &m) : module(m) {}

    sc_time axi_arbitration(tlm_generic_payload &transaction) const {
      SC_LOG_DELAY(&module, transaction, "AXI Arbitration",
                   module.axi_arbitration_delay);
      return module.axi_arbitration_delay;
    }

    sc_time axi_address(tlm_generic_payload &transaction) const {
      SC_LOG_DELAY(&module, transaction, "AXI Address", module.axi_clk_cycle);
      return module.axi_clk_cycle;
    }
  };

  DelayModel delays{*this};
};