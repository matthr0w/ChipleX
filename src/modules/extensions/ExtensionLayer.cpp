#include "modules/extensions/ExtensionLayer.h"
#include "modules/extensions/NOOPExtension.h"

// TODO:
// + Beat index in AxiBeat struct
// + DMA engine support
// + Interrupts

ExtensionLayer::ExtensionLayer(sc_module_name name,
                               ChipletConfig chiplet_config)
    : sc_module(name),
      axi_width(chiplet_config.node["axi"]["width"].as<unsigned>()),
      axi_in_up("axi_in_up", *this, &ExtensionLayer::nb_transport_fw_up,
                ARM::TLM::PROTOCOL_AXI4, axi_width),
      axi_out_up("axi_out_up", *this, &ExtensionLayer::nb_transport_bw_up,
                 ARM::TLM::PROTOCOL_AXI4, axi_width),
      axi_in_down("axi_in_down", *this, &ExtensionLayer::nb_transport_fw_down,
                  ARM::TLM::PROTOCOL_AXI4, axi_width),
      axi_out_down("axi_out_down", *this, &ExtensionLayer::nb_transport_bw_down,
                   ARM::TLM::PROTOCOL_AXI4, axi_width) {
  // Register AXI sockets
  up.fw_out = &axi_out_up;
  up.bw_out = &axi_in_up;
  down.fw_out = &axi_out_down;
  down.bw_out = &axi_in_down;

  // Register extensions
  extensions[SmartExtension::NOOP] = std::make_unique<NOOPExtension>();

  SC_METHOD(clk_posedge);
  sensitive << clk.pos();
}

void ExtensionLayer::clk_posedge() {
  // Clear AXI states
  up.clear_states();
  down.clear_states();
  up.clear_issued();
  down.clear_issued();

  // Process AXI incoming queues
  process_in_queues(up);
  process_in_queues(down);

  // Internal extension tick
  for (auto &ext : extensions)
    if (ext)
      ext->tick();

  // Route extension outputs
  route_extension_outputs();

  // Process AXI outgoing queues
  process_out_queues(up);
  process_out_queues(down);
}

// -------------------------------------------------------
// Transport Functions
// -------------------------------------------------------
tlm_sync_enum ExtensionLayer::nb_transport_fw_up(ARM::AXI::Payload &payload,
                                                 ARM::AXI::Phase &phase) {
  UserSignals user = UserSignals::decode(payload.user);

  // Bypass extensions
  if (user.extension_mask == 0)
    return axi_out_down.nb_transport_fw(payload, phase);

  switch (phase) {
  case ARM::AXI::AW_VALID:
    up.aw_in.push_back({&payload, ARM::AXI::AW_VALID, AxiDir::TO_IC});
    break;
  case ARM::AXI::W_VALID:
    up.w_in.push_back({&payload, ARM::AXI::W_VALID, AxiDir::TO_IC});
    break;
  case ARM::AXI::W_VALID_LAST:
    up.w_in.push_back({&payload, ARM::AXI::W_VALID_LAST, AxiDir::TO_IC});
    break;
  case ARM::AXI::B_READY:
    up.b_state = up.b_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::AR_VALID:
    up.ar_in.push_back({&payload, ARM::AXI::AR_VALID, AxiDir::TO_IC});
    break;
  case ARM::AXI::R_READY:
    up.r_state = up.r_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unexpected phase: "
                           << get_axi_phase_string(phase));
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum ExtensionLayer::nb_transport_bw_up(ARM::AXI::Payload &payload,
                                                 ARM::AXI::Phase &phase) {
  UserSignals user = UserSignals::decode(payload.user);

  // Bypass extensions
  if (user.extension_mask == 0)
    return axi_in_down.nb_transport_bw(payload, phase);

  switch (phase) {
  case ARM::AXI::AW_READY:
    up.aw_state = up.aw_state == REQ ? ACK : CLEAR;
    break;
  case ARM::AXI::W_READY:
    up.w_state = up.w_state == REQ ? ACK : CLEAR;
    break;
  case ARM::AXI::B_VALID:
    up.b_in.push_back({&payload, ARM::AXI::B_VALID, AxiDir::TO_IC});
    break;
  case ARM::AXI::AR_READY:
    up.ar_state = up.ar_state == REQ ? ACK : CLEAR;
    break;
  case ARM::AXI::R_VALID:
    up.r_in.push_back({&payload, ARM::AXI::R_VALID, AxiDir::TO_IC});
    break;
  case ARM::AXI::R_VALID_LAST:
    up.r_in.push_back({&payload, ARM::AXI::R_VALID_LAST, AxiDir::TO_IC});
    break;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unexpected phase: "
                           << get_axi_phase_string(phase));
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum ExtensionLayer::nb_transport_fw_down(ARM::AXI::Payload &payload,
                                                   ARM::AXI::Phase &phase) {
  UserSignals user = UserSignals::decode(payload.user);

  // Bypass extensions
  if (user.extension_mask == 0)
    return axi_out_up.nb_transport_fw(payload, phase);

  switch (phase) {
  case ARM::AXI::AW_VALID:
    down.aw_in.push_back({&payload, ARM::AXI::AW_VALID, AxiDir::TO_BUS});
    break;
  case ARM::AXI::W_VALID:
    down.w_in.push_back({&payload, ARM::AXI::W_VALID, AxiDir::TO_BUS});
    break;
  case ARM::AXI::W_VALID_LAST:
    down.w_in.push_back({&payload, ARM::AXI::W_VALID_LAST, AxiDir::TO_BUS});
    break;
  case ARM::AXI::B_READY:
    down.b_state = down.b_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::AR_VALID:
    down.ar_in.push_back({&payload, ARM::AXI::AR_VALID, AxiDir::TO_BUS});
    break;
  case ARM::AXI::R_READY:
    down.r_state = down.r_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unexpected phase: "
                           << get_axi_phase_string(phase));
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum ExtensionLayer::nb_transport_bw_down(ARM::AXI::Payload &payload,
                                                   ARM::AXI::Phase &phase) {
  UserSignals user = UserSignals::decode(payload.user);

  // Bypass extensions
  if (user.extension_mask == 0)
    return axi_in_up.nb_transport_bw(payload, phase);

  switch (phase) {
  case ARM::AXI::AW_READY:
    down.aw_state = down.aw_state == REQ ? ACK : CLEAR;
    break;
  case ARM::AXI::W_READY:
    down.w_state = down.w_state == REQ ? ACK : CLEAR;
    break;
  case ARM::AXI::B_VALID:
    down.b_in.push_back({&payload, ARM::AXI::B_VALID, AxiDir::TO_BUS});
    break;
  case ARM::AXI::AR_READY:
    down.ar_state = down.ar_state == REQ ? ACK : CLEAR;
    break;
  case ARM::AXI::R_VALID:
    down.r_in.push_back({&payload, ARM::AXI::R_VALID, AxiDir::TO_BUS});
    break;
  case ARM::AXI::R_VALID_LAST:
    down.r_in.push_back({&payload, ARM::AXI::R_VALID_LAST, AxiDir::TO_BUS});
    break;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unexpected phase: "
                           << get_axi_phase_string(phase));
  }

  return TLM_ACCEPTED;
}

// -------------------------------------------------------
// Helper Functions
// -------------------------------------------------------
void ExtensionLayer::route_extension_outputs() {
  for (auto &ext : extensions) {
    if (!ext || !ext->has_output())
      continue;

    const AxiBeat &beat = ext->peek();
    AxiSide &side = (beat.dir == AxiDir::TO_BUS) ? up : down;

    switch (beat.phase) {
    case ARM::AXI::AW_VALID:
      if (!side.aw_issued && side.aw_state == CLEAR) {
        side.aw_out.push_back(ext->pop());
        side.aw_issued = true;
      }
      break;

    case ARM::AXI::W_VALID:
    case ARM::AXI::W_VALID_LAST:
      if (!side.w_issued && side.w_state == CLEAR) {
        side.w_out.push_back(ext->pop());
        side.w_issued = true;
      }
      break;

    case ARM::AXI::B_VALID:
      if (!side.b_issued && side.b_state == CLEAR) {
        side.b_out.push_back(ext->pop());
        side.b_issued = true;
      }
      break;

    case ARM::AXI::AR_VALID:
      if (!side.ar_issued && side.ar_state == CLEAR) {
        side.ar_out.push_back(ext->pop());
        side.ar_issued = true;
      }
      break;

    case ARM::AXI::R_VALID:
    case ARM::AXI::R_VALID_LAST:
      if (!side.r_issued && side.r_state == CLEAR) {
        side.r_out.push_back(ext->pop());
        side.r_issued = true;
      }
      break;

    default:
      break;
    }
  }
}

void ExtensionLayer::process_in_queues(AxiSide &side) {
  auto push_ext = [&](std::deque<AxiBeat> &queue, ARM::AXI::Phase ready_phase) {
    if (queue.empty())
      return;

    AxiBeat beat = queue.front();
    ExtensionBase *ext = select_extension(
        UserSignals::decode(beat.payload->user).extension_mask);

    if (!ext || !ext->can_accept())
      return;

    queue.pop_front();
    ext->push(beat);

    if (ready_phase == ARM::AXI::B_READY || ready_phase == ARM::AXI::R_READY)
      side.fw_out->nb_transport_fw(*beat.payload, ready_phase);
    else
      side.bw_out->nb_transport_bw(*beat.payload, ready_phase);
  };

  push_ext(side.aw_in, ARM::AXI::AW_READY);
  push_ext(side.w_in, ARM::AXI::W_READY);
  push_ext(side.ar_in, ARM::AXI::AR_READY);
  push_ext(side.b_in, ARM::AXI::B_READY);
  push_ext(side.r_in, ARM::AXI::R_READY);
}

void ExtensionLayer::process_out_queues(AxiSide &side) {
  auto send_fw = [&](ChannelState &state, std::deque<AxiBeat> &queue) {
    if (state != CLEAR || queue.empty())
      return;

    state = REQ;
    AxiBeat &beat = queue.front();
    ARM::AXI::Phase phase = beat.phase;

    if (side.fw_out->nb_transport_fw(*beat.payload, phase) == TLM_UPDATED)
      state = ACK;
  };

  auto send_bw = [&](ChannelState &state, std::deque<AxiBeat> &queue) {
    if (state != CLEAR || queue.empty())
      return;

    state = REQ;
    AxiBeat &beat = queue.front();
    ARM::AXI::Phase phase = beat.phase;

    if (side.bw_out->nb_transport_bw(*beat.payload, phase) == TLM_UPDATED)
      state = ACK;
  };

  send_fw(side.aw_state, side.aw_out);
  send_fw(side.w_state, side.w_out);
  send_fw(side.ar_state, side.ar_out);
  send_bw(side.b_state, side.b_out);
  send_bw(side.r_state, side.r_out);
}