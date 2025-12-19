#pragma once

#include <memory>
#include <systemc>

#include "modules/DMAEngine.h"
#include "modules/extensions/ExtensionBase.h"
#include "modules/extensions/ExtensionIDs.h"
#include "setup/Types.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(ExtensionLayer), public DMAForwardInterface {
private:
  // -------------------------------------------------------
  // Config
  // -------------------------------------------------------
  const unsigned axi_width;

public:
  // -------------------------------------------------------
  // Signals
  // -------------------------------------------------------
  sc_in<bool> clk;

  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  ARM::AXI::SimpleTargetSocket<ExtensionLayer> axi_in_up;
  ARM::AXI::SimpleInitiatorSocket<ExtensionLayer> axi_out_up;
  ARM::AXI::SimpleTargetSocket<ExtensionLayer> axi_in_down;
  ARM::AXI::SimpleInitiatorSocket<ExtensionLayer> axi_out_down;

  ExtensionLayer(sc_module_name name, ChipletConfig chiplet_config,
                 DMAEngine * dma_engine);

private:
  // -------------------------------------------------------
  // Internal Declarations
  // -------------------------------------------------------
  enum ChannelState { CLEAR, REQ, ACK };

  struct AxiSide {
    // Incoming queues
    std::deque<AxiBeat> aw_in, w_in, b_in, ar_in, r_in;

    // Outgoing queues
    std::deque<AxiBeat> aw_out, w_out, b_out, ar_out, r_out;

    // Channel states
    ChannelState aw_state = CLEAR;
    ChannelState w_state = CLEAR;
    ChannelState b_state = CLEAR;
    ChannelState ar_state = CLEAR;
    ChannelState r_state = CLEAR;

    void clear_states() {
      if (aw_state == ACK) {
        aw_state = CLEAR;
        aw_out.pop_front();
      }

      if (w_state == ACK) {
        w_state = CLEAR;
        w_out.pop_front();
      }

      if (b_state == ACK) {
        b_state = CLEAR;
        b_out.pop_front();
      }

      if (ar_state == ACK) {
        ar_state = CLEAR;
        ar_out.pop_front();
      }

      if (r_state == ACK) {
        r_state = CLEAR;
        r_out.pop_front();
      }
    }

    bool aw_issued = false;
    bool w_issued = false;
    bool b_issued = false;
    bool ar_issued = false;
    bool r_issued = false;

    void clear_issued() {
      aw_issued = w_issued = b_issued = ar_issued = r_issued = false;
    }

    // Sockets
    ARM::AXI::SimpleInitiatorSocket<ExtensionLayer> *fw_out = nullptr;
    ARM::AXI::SimpleTargetSocket<ExtensionLayer> *bw_out = nullptr;
  };

  AxiSide up;
  AxiSide down;

  std::unordered_map<ARM::AXI::Payload *, int> payload_beat_index;

  std::array<std::unique_ptr<ExtensionBase>, SmartExtension::MAX> extensions{};

  // DMA engine
  DMAEngine *dma_engine = nullptr;
  int dma_vm_id = -1;

  void clk_posedge();

public:
  // -------------------------------------------------------
  // Transport Functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_up(ARM::AXI::Payload & payload,
                                   ARM::AXI::Phase & phase);
  tlm_sync_enum nb_transport_bw_up(ARM::AXI::Payload & payload,
                                   ARM::AXI::Phase & phase);
  tlm_sync_enum nb_transport_fw_down(ARM::AXI::Payload & payload,
                                     ARM::AXI::Phase & phase);
  tlm_sync_enum nb_transport_bw_down(ARM::AXI::Payload & payload,
                                     ARM::AXI::Phase & phase);
  // DMA engine
  tlm_sync_enum nb_transport_bw_axi(ARM::AXI::Payload & payload,
                                    ARM::AXI::Phase & phase) override {
    return nb_transport_bw_up(payload, phase);
  };

private:
  // -------------------------------------------------------
  // Helper Functions
  // -------------------------------------------------------
  ExtensionBase *select_extension(uint8_t mask) {
    if (mask == 0)
      return nullptr;

    // Select lowest-index active extension (single-extension mode)
    for (uint8_t i = 0; i < SmartExtension::MAX; ++i)
      if ((mask & (1u << i)) && extensions[i])
        return extensions[i].get();
    return nullptr;
  }

  void route_extension_outputs();

  void process_in_queues(AxiSide & side);
  void process_out_queues(AxiSide & side, bool use_dma = false);

  bool send_dma_request(ARM::AXI::Payload & payload, ARM::AXI4::Phase phase) {
    return dma_engine->forward_from_virtual(dma_vm_id, payload, phase);
  }
};