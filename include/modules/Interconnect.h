#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "ARM/TLM/arm_axi4.h"
#include "logging.h"

#include "common/Tracker.h"
#include "common/protocol/ChipletPayload.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Interconnect) {
public:
  sc_core::sc_in<bool> clock;

  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  ARM::AXI::SimpleTargetSocket<Interconnect> axi_tsocket;

  simple_target_socket_tagged<Interconnect> *phy_tsockets;
  simple_initiator_socket_tagged<Interconnect> *phy_isockets;

  simple_initiator_socket_tagged<Interconnect> *irq_sockets;

  Interconnect(sc_module_name name, unsigned chip_id, unsigned num_cores,
               unsigned num_interconnects, unsigned buffer_size,
               unsigned flit_size, unsigned overhead_size, double bandwidth,
               double distance, unsigned axi_width);
  ~Interconnect();

private:
  enum ChannelState { CLEAR, REQ, ACK };

  ChannelState r_state = CLEAR;
  ChannelState b_state = CLEAR;

  std::deque<ARM::AXI::Payload *> ar_queue;
  std::deque<ARM::AXI::Payload *> aw_queue;
  std::deque<ARM::AXI::Payload *> w_queue;
  std::deque<tlm_generic_payload *> phy_request_queue;

  ARM::AXI::Payload *r_outgoing = nullptr;
  ARM::AXI::Payload *b_outgoing = nullptr;

  std::vector<uint8_t> staging_buffer;
  size_t staging_buffer_ptr = 0;

  std::vector<std::vector<uint8_t>> tx_buffers;
  std::vector<std::vector<uint8_t>> rx_buffers;
  std::vector<size_t> tx_ptrs;
  std::vector<size_t> rx_ptrs;

  // fsm
  bool active_txn = false;
  bool r_flit_sent = false;
  bool wlast = false;

  unsigned beat_idx = 0;
  unsigned flit_header_bytes;
  unsigned flit_data_bytes;

  // flit metadata
  enum Command : uint8_t { READ_COMMAND = 0, WRITE_COMMAND = 1 };
  uint16_t flit_count = 0;
  uint16_t flit_id = 0;
  uint8_t request_id = 0;
  uint8_t core_id = 0;
  uint8_t source_id = 0;
  uint8_t destination_id = 0;
  Command command = READ_COMMAND;
  uint32_t address = UINT32_MAX;
  uint16_t size = 0;
  bool fixed_address = true;

  void clock_posedge();
  void clock_negedge();

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned chip_id;
  const unsigned buffer_size;
  const unsigned flit_size;
  const unsigned overhead_size;
  const double bandwidth;
  const double distance;
  const unsigned axi_width;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_axi(ARM::AXI::Payload & payload,
                                    ARM::AXI::Phase & phase);

  tlm_sync_enum nb_transport_fw_phy(int id, tlm_generic_payload &transaction,
                                    tlm_phase &phase, sc_time &delay);

  tlm_sync_enum nb_transport_bw_phy(int id, tlm_generic_payload &transaction,
                                    tlm_phase &phase, sc_time &delay);

  // -------------------------------------------------------
  // helper functions
  // -------------------------------------------------------
  void write_flit_header(uint8_t *flit_base, uint16_t flit_count,
                         uint16_t flit_id, uint8_t request_id, uint8_t core_id,
                         uint8_t source_id, uint8_t destination_id,
                         Command command, uint32_t address, uint16_t size,
                         bool fixed_address);

  // -------------------------------------------------------
  // debug functions
  // -------------------------------------------------------
  void dump_staging_buffer();
  void dump_phy_buffers();

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