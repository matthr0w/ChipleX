#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/Tracker.h"

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(InterconnectProtocol) {
public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<InterconnectProtocol> axi_tsocket;
  simple_initiator_socket<InterconnectProtocol> axi_isocket;

  simple_target_socket_tagged<InterconnectProtocol> *phy_tsockets;
  simple_initiator_socket_tagged<InterconnectProtocol> *phy_isockets;

  simple_initiator_socket_tagged<InterconnectProtocol> *irq_sockets;

  InterconnectProtocol(sc_module_name name, unsigned int chip_id,
                       unsigned int num_cores, unsigned int num_interconnects,
                       sc_time pre_delay, sc_time post_delay);
  ~InterconnectProtocol();

private:
  int current_interconnect;

  struct AXIRequest {
    tlm_generic_payload *transaction;
    tlm_phase *phase;
    sc_time *delay;
  };

  std::deque<AXIRequest> axi_request_queue;
  void process_axi_request_queue();

  struct PHYRequest {
    int interconnect_id;
    tlm_generic_payload *transaction;
    tlm_phase *phase;
    sc_time *delay;
  };

  std::deque<PHYRequest> phy_request_queue;
  void process_phy_request_queue();

  void send_flits(tlm_generic_payload & transaction);
  void send_axi_request(tlm_generic_payload & transaction);
  void send_phy_request(tlm_generic_payload & transaction);
  void send_irq(tlm_generic_payload & transaction, tlm_command command);

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int chip_id;
  const sc_time pre_delay;
  const sc_time post_delay;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event axi_req_evt;
  sc_event axi_resp_evt;
  sc_event phy_req_evt;
  sc_event phy_resp_evt;
  sc_event axi_transaction_done;
  sc_event phy_transaction_done;

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm_generic_payload> axi_peq;
  void process_axi_transaction();
  peq_with_get<tlm_generic_payload> phy_peq;
  void process_phy_transaction();

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_axi(tlm_generic_payload & transaction,
                                    tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw_axi(tlm_generic_payload & transaction,
                                    tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_fw_phy(int id, tlm_generic_payload &transaction,
                                    tlm_phase &phase, sc_time &delay);

  tlm_sync_enum nb_transport_bw_phy(int id, tlm_generic_payload &transaction,
                                    tlm_phase &phase, sc_time &delay);

  // -------------------------------------------------------
  // delay model
  // -------------------------------------------------------
  struct DelayModel {
  private:
    const InterconnectProtocol &module;

  public:
    DelayModel(const InterconnectProtocol &m) : module(m) {}

    sc_time pre_delay(tlm_generic_payload &transaction) const {
      SC_LOG_DELAY(&module, transaction, "Preprocessing", module.pre_delay);
      return module.pre_delay;
    }

    sc_time post_delay(tlm_generic_payload &transaction) const {
      SC_LOG_DELAY(&module, transaction, "Postprocessing", module.post_delay);
      return module.post_delay;
    }
  };

  DelayModel delays{*this};
};