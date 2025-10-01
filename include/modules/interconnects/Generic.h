#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "logging.h"

#include "ARM/TLM/arm_axi4.h"

#include "common/System.h"

#include "modules/DMAEngine.h"
#include "modules/interconnects/Base.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(GenericInterconnect), public InterconnectBase,
    public VirtualAXIInitiatorIF {
private:
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
  sc_in<bool> axi_clock;
  sc_in<bool> protocol_clock;
  sc_in<bool> phy_clock;

  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  ARM::AXI::SimpleTargetSocket<GenericInterconnect> axi_tsocket;
  ARM::AXI::SimpleInitiatorSocket<GenericInterconnect> axi_isocket;

  simple_target_socket_tagged<GenericInterconnect> *phy_tsockets;
  simple_initiator_socket_tagged<GenericInterconnect> *phy_isockets;

  simple_initiator_socket_tagged<GenericInterconnect> *irq_sockets;

  GenericInterconnect(
      sc_module_name name, unsigned chiplet_id, ChipletConfig chiplet_config,
      InterconnectConfig interconnect_config, DMAEngine *dma_engine);
  ~GenericInterconnect();

  // InterconnectBase
  void bind_clock(sc_clock & clk) override;
  // DMAEngine
  tlm_sync_enum nb_transport_bw_axi(ARM::AXI::Payload & payload,
                                    ARM::AXI::Phase & phase) override;

private:
  // -------------------------------------------------------
  // Internal Declarations
  // -------------------------------------------------------
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

  // Flit metadata
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

  // State variables
  bool axi_active_tx = false;
  bool axi_active_rx = false;
  int axi_active_rx_idx = -1;
  int axi_active_flit_id = -1;
  bool axi_active_read = false;
  bool axi_rlast_beat = false;
  bool axi_wlast_beat = false;
  bool protocol_rreq_flit_sent = false;
  std::vector<bool> phy_active_tx;

  // DMA engine
  DMAEngine *dma_engine = nullptr;
  int dma_vm_id = -1;

  void axi_clock_posedge();
  void protocol_clock_posedge();
  void phy_clock_posedge();

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

      flit_transfer_delay =
          sc_time(static_cast<double>(module.flit_size) /
                      config["interconnect"]["bandwidth"].as<double>(),
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
        max_attempts =
            config["interconnect_protocol"]["retries"].as<unsigned>() +
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
          delay += sc_time(
              config["interconnect_protocol"]["fec_delay"].as<unsigned>(),
              SC_NS);
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
  // Debug Functions
  // -------------------------------------------------------
  void dump_staging_buffer();
  void dump_tx_buffers();
  void dump_rx_buffers();
};