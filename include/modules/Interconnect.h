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

#include "modules/DMAEngine.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Interconnect), public VirtualAXIInitiatorIF {
public:
  sc_in<bool> axi_clock;
  sc_in<bool> protocol_clock;
  sc_in<bool> phy_clock;

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

  Interconnect(sc_module_name name, unsigned chip_id, unsigned axi_width,
               unsigned num_cores, unsigned num_interconnects,
               unsigned flit_size, unsigned overhead_size,
               unsigned staging_buffer_size, unsigned link_buffer_size,
               double bandwidth, double distance, DMAEngine *dma_engine);
  ~Interconnect();

private:
  enum ChannelState { CLEAR, REQ, ACK };

  ChannelState r_state = CLEAR;
  ChannelState b_state = CLEAR;

  std::deque<ARM::AXI::Payload *> ar_queue;
  std::deque<ARM::AXI::Payload *> aw_queue;
  std::deque<ARM::AXI::Payload *> w_queue;

  ARM::AXI::Payload *r_outgoing = nullptr;
  ARM::AXI::Payload *b_outgoing = nullptr;

  struct PHYRequest {
    int interconnect_id;
    tlm_generic_payload *transaction;
  };

  std::deque<PHYRequest> phy_queue;

  std::vector<uint8_t> staging_buffer;
  size_t staging_buffer_ptr = 0;

  std::vector<std::vector<uint8_t>> tx_buffers;
  std::vector<std::vector<uint8_t>> rx_buffers;
  std::vector<size_t> tx_ptrs;
  std::vector<size_t> rx_ptrs;

  unsigned beat_idx = 0;
  size_t flit_header_bytes;
  size_t flit_data_bytes;

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

  void axi_clock_posedge();
  void protocol_clock_posedge();
  void phy_clock_posedge();

  // -------------------------------------------------------
  // dma engine
  // -------------------------------------------------------
  DMAEngine *dma_engine = nullptr;
  int dma_vm_id = -1;

  // -------------------------------------------------------
  // state variables
  // -------------------------------------------------------
  bool axi_active_tx = false;
  bool axi_active_rx = false;
  int axi_active_rx_idx = -1;
  int axi_active_flit_id = -1;
  bool axi_active_read = false;
  bool axi_rlast_beat = false;
  bool axi_wlast_beat = false;
  bool protocol_rreq_flit_sent = false;
  std::vector<bool> phy_active_tx;

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned chip_id;
  const unsigned axi_width;
  const unsigned flit_size;
  const unsigned overhead_size;
  const unsigned staging_buffer_size;
  const unsigned link_buffer_size;
  const double bandwidth;
  const double distance;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_axi(ARM::AXI::Payload & payload,
                                    ARM::AXI::Phase & phase);

public: // needs to be public for dma engine
  tlm_sync_enum nb_transport_bw_axi(ARM::AXI::Payload & payload,
                                    ARM::AXI::Phase & phase);

private:
  tlm_sync_enum nb_transport_fw_phy(int id, tlm_generic_payload &transaction,
                                    tlm_phase &phase, sc_time &delay);

  tlm_sync_enum nb_transport_bw_phy(int id, tlm_generic_payload &transaction,
                                    tlm_phase &phase, sc_time &delay);

  // -------------------------------------------------------
  // helper functions
  // -------------------------------------------------------
  struct FlitHeader {
    uint16_t flit_count;
    uint16_t flit_id;
    uint8_t request_id;
    uint8_t core_id;
    uint8_t source_id;
    uint8_t destination_id;
    Command command;
    uint32_t address;
    uint16_t size;
    bool fixed_address;
  };

  size_t read_flit_header(uint8_t *flit_base, FlitHeader &flit_header);
  size_t write_flit_header(uint8_t *flit_base, FlitHeader &flit_header);

  bool send_dma_request(ARM::AXI::Payload & payload) {
    return dma_engine->forward_from_virtual(dma_vm_id, payload);
  }

  void send_irq(ARM::AXI::Payload & payload);

  // -------------------------------------------------------
  // debug functions
  // -------------------------------------------------------
  void dump_staging_buffer();
  void dump_tx_buffers();
  void dump_rx_buffers();

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