#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "logging.h"

#include "ARM/TLM/arm_axi4.h"
#include "common/Statistics.h"
#include "modules/DMAEngine.h"
#include "modules/interconnects/InterconnectBase.h"
#include "modules/interconnects/generic/Types.h"
#include "setup/Types.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(GenericInterconnect), public InterconnectBase,
    public VirtualAXIInitiatorIF {
private:
  void end_of_simulation() override;

  // -------------------------------------------------------
  // Config
  // -------------------------------------------------------
  const unsigned chiplet_id;
  const unsigned num_cores;
  const unsigned num_links;
  const unsigned axi_width;
  const unsigned flit_size;
  const unsigned overhead_size;
  const unsigned staging_buffer_size;
  const unsigned link_buffer_size;
  const std::vector<ChipletConnectionConfig> connections;

public:
  // -------------------------------------------------------
  // Signals
  // -------------------------------------------------------
  sc_in<bool> protocol_clk;
  sc_in<bool> phy_clk;

  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  ARM::AXI::SimpleTargetSocket<GenericInterconnect> axi_in;
  ARM::AXI::SimpleInitiatorSocket<GenericInterconnect> axi_out;

  simple_target_socket_tagged<GenericInterconnect> *phy_in;
  simple_initiator_socket_tagged<GenericInterconnect> *phy_out;

  simple_initiator_socket_tagged<GenericInterconnect> *irq_sockets;

  GenericInterconnect(sc_module_name name, unsigned chiplet_id,
                      ChipletConfig chiplet_config,
                      InterconnectConfig interconnect_config,
                      unsigned num_cores, DMAEngine *dma_engine);
  ~GenericInterconnect();

  // InterconnectBase
  void bind_clocks(Clocks & clocks) override;
  // DMAEngine
  tlm_sync_enum nb_transport_bw_axi(ARM::AXI::Payload & payload,
                                    ARM::AXI::Phase & phase) override;

private:
  // -------------------------------------------------------
  // Internal Declarations
  // -------------------------------------------------------
  StatManager &stats = StatManager::instance();

  enum ChannelState { CLEAR, REQ, ACK };

  ChannelState aw_state = CLEAR;
  ChannelState w_state = CLEAR;
  ChannelState b_state = CLEAR;
  ChannelState ar_state = CLEAR;
  ChannelState r_state = CLEAR;

  std::deque<ARM::AXI::Payload *> aw_queue_in;
  std::deque<ARM::AXI::Payload *> aw_queue_out;
  std::deque<ARM::AXI::Payload *> w_queue_in;
  std::deque<ARM::AXI::Payload *> w_queue_out;
  std::deque<ARM::AXI::Payload *> b_queue_in;
  std::deque<ARM::AXI::Payload *> b_queue_out;
  std::deque<ARM::AXI::Payload *> ar_queue_in;
  std::deque<ARM::AXI::Payload *> ar_queue_out;
  std::deque<ARM::AXI::Payload *> r_queue_in;
  std::deque<ARM::AXI::Payload *> r_queue_out;

  unsigned r_beat_count = 0;
  unsigned flit_r_beat_count = 0;
  unsigned w_beat_count = 0;
  unsigned flit_w_beat_count = 0;

  AxiTransaction axi_transaction;

  std::unordered_map<PayloadKey, ARM::AXI::Payload *, PayloadKeyHash>
      subordinate_payloads;
  std::unordered_map<PayloadKey, ARM::AXI::Payload *, PayloadKeyHash>
      manager_payloads;

  std::deque<PhyRequest> phy_queue;
  std::vector<bool> phy_active_tx;

  std::vector<uint8_t> staging_buffer;
  size_t staging_buffer_ptr = 0;

  std::vector<std::vector<uint8_t>> tx_buffers;
  std::vector<std::vector<uint8_t>> rx_buffers;
  std::vector<size_t> tx_ptrs;
  std::vector<size_t> rx_ptrs;

  size_t flit_header_bytes;
  size_t flit_data_bytes;

  bool flush_staging_buffer = false;
  bool reset_axi_channel = false;

  // DMA engine
  DMAEngine *dma_engine = nullptr;
  int dma_vm_id = -1;

  void clk_posedge_axi();
  void clk_posedge_protocol();
  void clk_posedge_phy();

  // -------------------------------------------------------
  // Delay Model
  // -------------------------------------------------------
  struct Transfer {
    sc_time delay;
    bool success;
  };

  struct DelayModel {
  private:
    const GenericInterconnect &module;

  public:
    DelayModel(const GenericInterconnect &m) : module(m) {}

    Transfer transfer_delay(int id, tlm_generic_payload &transaction) const {
      ChipletConnectionConfig connection = module.connections[id];
      InterconnectType interconnect = connection.type;
      YAML::Node config = connection.config;

      sc_time delay = SC_ZERO_TIME;
      sc_time flit_transfer_delay = SC_ZERO_TIME;
      sc_time wire_propagation_delay = SC_ZERO_TIME;

      flit_transfer_delay = sc_time(static_cast<double>(module.flit_size) /
                                        config["phy"]["bandwidth"].as<double>(),
                                    SC_NS);

      // Wire propagation delay based on wire length
      wire_propagation_delay =
          sc_time(connection.wire_length * wire_ps_per_mm, SC_PS);

      delay = flit_transfer_delay + wire_propagation_delay;
      sc_time base_transfer_delay = delay;

      double scaled_ber =
          std::clamp(bit_error_rate * connection.ber_scalar, 0.0, 1.0);

      double prob_bad_transfer =
          1.0 - std::pow(1.0 - scaled_ber, module.flit_size * 8);

      int max_attempts = 1;
      switch (interconnect.type) {
      case InterconnectType::Type::UCIe:
        max_attempts = config["protocol"]["retries"].as<unsigned>() +
                       1; // + 1 for first try
        break;
      default:
        break;
      }

      // Bit error simulation
      bool transfer_successful = false;
      for (int attempt = 0; attempt < max_attempts; ++attempt) {
        if (bit_error_dist(bit_error_gen) >= prob_bad_transfer) {
          // No bit error
          transfer_successful = true;
          break;
        }

        SC_LOG_ERROR(&module,
                     "Bit error on attempt " + std::to_string(attempt + 1));

        switch (interconnect.type) {
        case InterconnectType::Type::PCIe:
          // FEC penalty
          delay +=
              sc_time(config["protocol"]["fec_delay"].as<unsigned>(), SC_NS);
          // Assuming FEC handles it
          transfer_successful = true;
          break;
        case InterconnectType::Type::UCIe:
          // Retry penalty
          delay += base_transfer_delay;
        default:;
        }
      }

      if (!transfer_successful)
        SC_LOG_ERROR(&module, "Transfer failed");

      SC_LOG_DELAY(&module, "Die to Die Transfer", delay);
      return {delay, transfer_successful};
    }
  };

  DelayModel delays{*this};

  // -------------------------------------------------------
  // Transport Functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_axi(ARM::AXI::Payload & payload,
                                    ARM::AXI::Phase & phase);

  tlm_sync_enum nb_transport_fw_phy(int id, tlm_generic_payload &transaction,
                                    tlm_phase &phase, sc_time &delay);

  tlm_sync_enum nb_transport_bw_phy(int id, tlm_generic_payload &transaction,
                                    tlm_phase &phase, sc_time &delay);

  // -------------------------------------------------------
  // Helper Functions
  // -------------------------------------------------------
  void clear_axi_states();
  void handle_axi_channels();
  void send_axi_beats();

  Flit read_flit_from_buffer(const uint8_t *src);
  void write_flit_to_buffer(uint8_t *dest, const Flit &flit,
                            size_t flit_payload_bytes,
                            size_t flit_padding_bytes);

  void forward_flit(unsigned rx_idx, uint8_t dest_id);
  void process_flit(unsigned rx_idx, Flit &flit);

  void send_irq(ARM::AXI::Payload & payload);

  bool send_dma_request(ARM::AXI::Payload & payload,
                        ARM::AXI4::Channel channel) {
    return dma_engine->forward_from_virtual(dma_vm_id, payload, channel);
  }

  void erase_payload(
      std::unordered_map<PayloadKey, ARM::AXI::Payload *, PayloadKeyHash> &
          payload_map,
      ARM::AXI::Payload * payload);

  // -------------------------------------------------------
  // Debug Functions
  // -------------------------------------------------------
  void dump_staging_buffer();
  void dump_tx_buffers();
  void dump_rx_buffers();
};