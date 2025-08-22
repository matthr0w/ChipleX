#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(AXISubordinate) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<AXISubordinate> tsocket;
  simple_initiator_socket<AXISubordinate> isocket;

  AXISubordinate(sc_module_name name, unsigned int axi_channel_width,
                 sc_time axi_clk_cycle);

private:
  struct Request {
    tlm_generic_payload *transaction;
    tlm_phase *phase;
    sc_time *delay;
  };

  std::deque<Request> read_channel;
  void process_read_channel();
  std::deque<Request> write_channel;
  void process_write_channel();

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int axi_channel_width;
  const sc_time axi_clk_cycle;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event read_req_evt;
  sc_event write_req_evt;

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
    const AXISubordinate &module;

  public:
    DelayModel(const AXISubordinate &m) : module(m) {}

    sc_time axi_request(tlm_generic_payload &transaction) const {
      unsigned int num_beats = 0;

      if (transaction.is_read()) {
        // ---------------------------
        // read response: address beat
        // ---------------------------
        num_beats = 1;

      } else if (transaction.is_write()) {
        // ---------------------------
        // write response: address + burst beats
        // ---------------------------
        // AxLEN + 1 + address beat
        num_beats =
            static_cast<ChipletPayload *>(&transaction)->get_axi_length() + 2;
      }

      sc_time delay = num_beats * module.axi_clk_cycle;

      SC_LOG_DELAY(&module, transaction, "AXI Request", delay);
      return delay;
    }
  };

  DelayModel delays{*this};
};