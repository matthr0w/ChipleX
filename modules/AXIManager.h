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
      sc_time delay = SC_ZERO_TIME;
      unsigned int total_bytes = 0;

      if (transaction.is_read()) {
        // ---------------------------
        // read response: extension + data
        // ---------------------------
        unsigned int ext_bytes =
            static_cast<ChipletPayload *>(&transaction)->get_ext_length();
        unsigned int data_bytes = transaction.get_data_length();

        total_bytes = ext_bytes + data_bytes;

      } else if (transaction.is_write()) {
        // ---------------------------
        // write response: address only
        // ---------------------------
        total_bytes = sizeof(uint32_t);
      }

      unsigned int num_cycles =
          (total_bytes * 8 + module.axi_channel_width - 1) /
          module.axi_channel_width;

      delay = num_cycles * module.axi_clk_cycle;

      SC_LOG_DELAY(&module, transaction, "AXI Response", delay);
      return delay;
    }
  };

  DelayModel delays{*this};
};