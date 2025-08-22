#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(AXIManager) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<AXIManager> tsocket;
  simple_initiator_socket<AXIManager> isocket;

  AXIManager(sc_module_name name, unsigned int axi_channel_width,
             sc_time axi_clk_cycle);

private:
  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int axi_channel_width;
  const sc_time axi_clk_cycle;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);

  // -------------------------------------------------------
  // delay model
  // -------------------------------------------------------
  struct DelayModel {
  private:
    const AXIManager &module;

  public:
    DelayModel(const AXIManager &m) : module(m) {}

    sc_time axi_response(tlm_generic_payload &transaction) const {
      unsigned int num_beats = 0;

      if (transaction.is_read()) {
        // ---------------------------
        // read response: burst beats
        // ---------------------------
        // AxLEN + 1
        num_beats =
            static_cast<ChipletPayload *>(&transaction)->get_axi_length() + 1;

      } else if (transaction.is_write()) {
        // ---------------------------
        // write response: response beat
        // ---------------------------
        num_beats = 1;
      };

      sc_time delay = num_beats * module.axi_clk_cycle;

      SC_LOG_DELAY(&module, transaction, "AXI Response", delay);
      return delay;
    }
  };

  DelayModel delays{*this};
};