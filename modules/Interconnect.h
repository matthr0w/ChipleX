#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/Tracker.h"
#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Interconnect) {
public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;
  BufferUsageTracker tx_tracker;
  BufferUsageTracker rx_tracker;
  unsigned int incoming_flits;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<Interconnect> protocol_tsocket;
  simple_initiator_socket<Interconnect> protocol_isocket;

  simple_target_socket<Interconnect> phy_tsocket;
  simple_initiator_socket<Interconnect> phy_isocket;

  Interconnect(sc_module_name name, unsigned int buffer_size,
               unsigned int flit_size, double bandwidth, double distance);

private:
  void process_tx_buffer();
  void process_rx_buffer();

  std::deque<tlm_generic_payload *> tx_buffer;
  std::deque<tlm_generic_payload *> rx_buffer;
  unsigned int tx_buffer_used_bytes;
  unsigned int rx_buffer_used_bytes;

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int buffer_size;
  const unsigned int flit_size;
  const double bandwidth;
  const double distance;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event tx_buffer_in_event;
  sc_event tx_buffer_out_event;
  sc_event rx_buffer_in_event;
  sc_event rx_buffer_out_event;
  sc_event protocol_transaction_done;
  sc_event phy_transaction_done;

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm_generic_payload> protocol_peq;
  void process_protocol_transaction();
  peq_with_get<tlm_generic_payload> phy_peq;
  void process_phy_transaction();

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_protocol(tlm_generic_payload & transaction,
                                         tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw_protocol(tlm_generic_payload & transaction,
                                         tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_fw_phy(tlm_generic_payload & transaction,
                                    tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw_phy(tlm_generic_payload & transaction,
                                    tlm_phase & phase, sc_time & delay);

  // -------------------------------------------------------
  // delay model
  // -------------------------------------------------------
  struct DelayModel {
  private:
    const Interconnect &module;

  public:
    DelayModel(const Interconnect &m) : module(m) {}

    sc_time transfer_delay(tlm_generic_payload &transaction) const {
      static const Config &interconnect_config =
          ConfigRegistry::instance().get("Interconnect");

      sc_time delay = SC_ZERO_TIME;
      sc_time flit_transfer_delay = SC_ZERO_TIME;
      sc_time wire_propagation_delay = SC_ZERO_TIME;

      flit_transfer_delay = sc_time(
          static_cast<double>(module.flit_size) / module.bandwidth, SC_NS);

      // wire propagation delay based on distance
      wire_propagation_delay = sc_time(module.distance * wire_ps_per_mm, SC_PS);

      delay = flit_transfer_delay + wire_propagation_delay;
      sc_time base_transfer_delay = delay;

      double prob_bad_transfer =
          1.0 - std::pow(1.0 - bit_error_rate, module.flit_size * 8);

      int max_attempts = 1;
      switch (connection_type) {
      case ConnectionType::UCIe:
        max_attempts = interconnect_config.get<unsigned int>(
                           "interconnect_protocol.retries") +
                       1; // + 1 for first try
        break;
      default:
        break;
      }

      bool transfer_successful = false;

      for (int attempt = 0; attempt < max_attempts; ++attempt) {
        TransmissionTracker::instance().record_transmission(
            base_transfer_delay);

        if (bit_error_dist(bit_error_gen) >= prob_bad_transfer) {
          // no bit error
          transfer_successful = true;
          break;
        }

        SC_LOG_ERROR(&module, transaction,
                     "Bit error on attempt " + std::to_string(attempt + 1));

        switch (connection_type) {
        case ConnectionType::PCIe:
          // forward error correction penalty
          delay += interconnect_config.get<sc_time>(
              "interconnect_protocol.fec_delay");
          TransmissionTracker::instance().record_attempt();
          // assuming FEC handles it
          transfer_successful = true;
          break;
        case ConnectionType::UCIe:
          // retry penalty
          delay += base_transfer_delay;
          TransmissionTracker::instance().record_attempt();
        default:;
        }
      }

      static_cast<ChipletPayload *>(&transaction)
          ->set_transfer_result(transfer_successful);

      if (!transfer_successful) {
        SC_LOG_ERROR(&module, transaction, "Transfer failed");
      }

      SC_LOG_DELAY(&module, transaction, "Die to Die Transfer", delay);
      return delay;
    }
  };

  DelayModel delays{*this};
};