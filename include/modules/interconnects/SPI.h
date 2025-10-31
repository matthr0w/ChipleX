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
#include "setup/Types.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(SPI), public InterconnectBase, public VirtualAXIInitiatorIF {
private:
  void end_of_simulation() override;

  // -------------------------------------------------------
  // Config
  // -------------------------------------------------------
  const unsigned chiplet_id;
  const unsigned num_cores;
  const unsigned num_links;
  const unsigned axi_width;
  const std::vector<ChipletConnectionConfig> connections;

public:
  // -------------------------------------------------------
  // Signals
  // -------------------------------------------------------
  sc_in<bool> clk;

  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  ARM::AXI::SimpleTargetSocket<SPI> axi_in;
  ARM::AXI::SimpleInitiatorSocket<SPI> axi_out;

  simple_target_socket_tagged<SPI> *links_in;
  simple_initiator_socket_tagged<SPI> *links_out;

  simple_initiator_socket_tagged<SPI> *irq_sockets;

  SPI(sc_module_name name, unsigned chiplet_id, ChipletConfig chiplet_config,
      InterconnectConfig interconnect_config, unsigned num_cores,
      DMAEngine *dma_engine);
  ~SPI();

  // InterconnectBase
  void bind_clock(sc_clock & clk) override;
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

  struct LinkRequest {
    int link_id;
    ARM::AXI::Payload *payload;
  };

  std::deque<ARM::AXI::Payload *> aw_queue_in;
  std::deque<LinkRequest> aw_queue_out;
  std::deque<ARM::AXI::Payload *> w_queue_in;
  std::deque<LinkRequest> w_queue_out;
  std::deque<ARM::AXI::Payload *> b_queue_in;
  std::deque<LinkRequest> b_queue_out;
  std::deque<ARM::AXI::Payload *> ar_queue_in;
  std::deque<LinkRequest> ar_queue_out;
  std::deque<ARM::AXI::Payload *> r_queue_in;
  std::deque<LinkRequest> r_queue_out;

  std::deque<LinkRequest> links_queue;

  std::unordered_map<ARM::AXI::Payload *, unsigned> payload_beats;
  std::unordered_map<ARM::AXI::Payload *, bool> payloads_in;
  LinkRequest link_req_out = {-1, nullptr};

  std::vector<bool> active_links;
  bool active_transfer = false;

  // DMA engine
  DMAEngine *dma_engine = nullptr;
  int dma_vm_id = -1;

  void clk_posedge();

  // -------------------------------------------------------
  // Delay Model
  // -------------------------------------------------------
  struct Transfer {
    sc_time delay;
    bool success;
  };

  struct DelayModel {
  private:
    const SPI &module;

  public:
    DelayModel(const SPI &m) : module(m) {}

    Transfer transfer_delay(int id, tlm_generic_payload &transaction) const {
      ChipletConnectionConfig connection = module.connections[id];
      InterconnectType interconnect = connection.type;
      YAML::Node config = connection.config;

      sc_time delay = SC_ZERO_TIME;
      sc_time beat_transfer_delay = SC_ZERO_TIME;
      sc_time wire_propagation_delay = SC_ZERO_TIME;

      sc_time clk_cycle(config["clk_cycle"].as<unsigned>(), SC_NS);
      bool ddr = config["ddr"].as<bool>();
      unsigned num_lanes = config["num_lanes"].as<unsigned>();

      unsigned num_cycles =
          ((module.axi_width + num_lanes - 1) / num_lanes + (ddr ? 1 : 0)) /
          (ddr ? 2 : 1);

      beat_transfer_delay = num_cycles * clk_cycle;

      // Wire propagation delay based on wire length
      wire_propagation_delay =
          sc_time(connection.wire_length * wire_ps_per_mm, SC_PS);

      delay = beat_transfer_delay + wire_propagation_delay;

      // Bit error simulation
      double scaled_ber =
          std::clamp(bit_error_rate * connection.ber_scalar, 0.0, 1.0);
      double prob_bad_transfer =
          1.0 - std::pow(1.0 - scaled_ber, module.axi_width);
      bool transfer_successful =
          (bit_error_dist(bit_error_gen) >= prob_bad_transfer);

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

  tlm_sync_enum nb_transport_fw_link(int id, tlm_generic_payload &transaction,
                                     tlm_phase &phase, sc_time &delay);

  tlm_sync_enum nb_transport_bw_link(int id, tlm_generic_payload &transaction,
                                     tlm_phase &phase, sc_time &delay);

  // -------------------------------------------------------
  // Helper Functions
  // -------------------------------------------------------
  void clear_axi_states();
  void send_axi_beats();

  void send_irq(ARM::AXI::Payload & payload);

  bool send_link_request(ARM::AXI::Payload & payload);

  bool send_dma_request(ARM::AXI::Payload & payload,
                        ARM::AXI4::Channel channel) {
    return dma_engine->forward_from_virtual(dma_vm_id, payload, channel);
  }

  void register_payload_in(ARM::AXI::Payload & payload);
  void unregister_payload_in(ARM::AXI::Payload & payload);
  bool is_response(ARM::AXI::Payload & payload);

  void register_beat_count(ARM::AXI::Payload & payload);
  void unregister_beat_count(ARM::AXI::Payload & payload);
  void increment_beat_count(ARM::AXI::Payload & payload);
  int get_beat_count(ARM::AXI::Payload & payload);
};