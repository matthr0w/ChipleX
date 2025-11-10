#include "modules/DMAEngine.h"

#include "logging.h"

DMAEngine::DMAEngine(sc_module_name name, YAML::Node config)
    : sc_module(name), axi_width(config["axi"]["width"].as<unsigned>()),
      tsocket("tsocket", *this, &DMAEngine::nb_transport_fw,
              ARM::TLM::PROTOCOL_AXI4, axi_width),
      isocket("isocket", *this, &DMAEngine::nb_transport_bw,
              ARM::TLM::PROTOCOL_AXI4, axi_width) {
  SC_METHOD(clk_posedge);
  sensitive << clk.pos();
  dont_initialize();

  SC_METHOD(clk_negedge);
  sensitive << clk.neg();
  dont_initialize();
}

int DMAEngine::register_virtual_initiator(DMAForwardInterface *owner) {
  int vm_id = static_cast<int>(owners.size());
  owners.push_back(owner);
  return vm_id;
}

void DMAEngine::unregister_virtual_initiator(int vm_id) {
  if (vm_id < 0 || static_cast<size_t>(vm_id) >= owners.size())
    return;
  owners[vm_id] = nullptr;
}

bool DMAEngine::forward_from_virtual(int vm_id, ARM::AXI::Payload &payload,
                                     ARM::AXI::Channel channel) {
  SC_LOG_ASSERT(this, vm_id >= 0 && vm_id < owners.size(),
                "DMA Forward Request: Unregistered virtual");
  DMAForwardInterface *owner = owners[vm_id];
  SC_LOG_ASSERT(this, owner != nullptr,
                "DMA Forward Request: Unregistered virtual");

  payload_owner_map[&payload] = vm_id;

  switch (channel) {
  case ARM::AXI::Channel::CHANNEL_AW:
    if (state == DMAEngineState::Idle) {
      state = DMAEngineState::WriteForward;
      aw_queue_out.push_back(&payload);
      return true;
    }
    break;
  case ARM::AXI::Channel::CHANNEL_W:
    if (state == DMAEngineState::WriteForward) {
      state = DMAEngineState::WriteForward;
      w_queue_out.push_back(&payload);
      return true;
    }
    break;
  case ARM::AXI::Channel::CHANNEL_AR:
    if (state == DMAEngineState::Idle) {
      state = DMAEngineState::ReadForward;
      ar_queue_out.push_back(&payload);
      return true;
    }
    break;
  default:
    return false;
  }

  return false;
}

void DMAEngine::clk_posedge() {
  // AW channel
  if (aw_state == ACK) {
    aw_state = CLEAR;
    aw_queue_out.pop_front();
  }

  // W channel
  if (w_state == ACK) {
    w_state = CLEAR;
    w_beat_count++;
    if (w_beat_count == w_queue_out.front()->get_beat_count())
      w_beat_count = 0;
    w_queue_out.pop_front();
  }

  // B channel
  if (b_state == ACK) {
    b_state = CLEAR;
    b_outgoing = nullptr;
  }

  // AR channel
  if (ar_state == ACK) {
    ar_state = CLEAR;
    ar_queue_out.pop_front();
  }

  // State machine
  if (!aw_queue_in.empty() && state == DMAEngineState::Idle) {
    ARM::AXI::Phase phase = ARM::AXI::AW_READY;
    tsocket.nb_transport_bw(*aw_queue_in.front(), phase);
    state = DMAEngineState::ReadFetch;
  } else if (!w_queue_in.empty() && state == DMAEngineState::ReadFetch) {
    DMARequest req;
    w_queue_in.front()->write_out(reinterpret_cast<uint8_t *>(&req));
    w_queue_in.pop_front();
    b_outgoing = aw_queue_in.front();
    aw_queue_in.pop_front();

    current_request = req;
    issue_fetch_read(current_request);
  }
}

void DMAEngine::clk_negedge() {
  // AW channel
  if (aw_state == CLEAR && !aw_queue_out.empty()) {
    aw_state = REQ;
    ARM::AXI::Payload *payload = aw_queue_out.front();
    ARM::AXI::Phase phase = ARM::AXI::AW_VALID;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::AW_READY,
                    "AXI TLM Protocol: Unexpected phase");
      aw_state = ACK;
      // Backward to virtual initiator
      int vm_id = payload_owner_map.find(payload)->second;
      if (vm_id != internal_vm_id)
        owners[vm_id]->nb_transport_bw_axi(*payload, phase);
    }
  }

  // W channel
  if (w_state == CLEAR && !w_queue_out.empty()) {
    w_state = REQ;
    ARM::AXI::Payload *payload = w_queue_out.front();
    ARM::AXI::Phase phase = (w_beat_count + 1 == payload->get_beat_count())
                                ? ARM::AXI::W_VALID_LAST
                                : ARM::AXI::W_VALID;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::W_READY,
                    "AXI TLM Protocol: Unexpected phase");
      w_state = ACK;
      // Backward to virtual initiator
      int vm_id = payload_owner_map.find(payload)->second;
      if (vm_id != internal_vm_id)
        owners[vm_id]->nb_transport_bw_axi(*payload, phase);
    }
  }

  // B channel
  if (b_state == CLEAR && b_outgoing) {
    b_state = REQ;
    ARM::AXI::Phase phase = ARM::AXI::B_VALID;
    tlm_sync_enum reply = tsocket.nb_transport_bw(*b_outgoing, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::B_READY,
                    "AXI TLM Protocol: Unexpected phase");
      b_state = ACK;
    }
  }

  // AR channel
  if (ar_state == CLEAR && !ar_queue_out.empty()) {
    ar_state = REQ;
    ARM::AXI::Payload *payload = ar_queue_out.front();
    ARM::AXI::Phase phase = ARM::AXI::AR_VALID;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::AR_READY,
                    "AXI TLM Protocol: Unexpected phase");
      ar_state = ACK;
      // Backward to virtual initiator
      int vm_id = payload_owner_map.find(payload)->second;
      if (vm_id != internal_vm_id)
        owners[vm_id]->nb_transport_bw_axi(*payload, phase);
    }
  }
}

// -------------------------------------------------------
// Transport Functions
// -------------------------------------------------------
tlm_sync_enum DMAEngine::nb_transport_fw(ARM::AXI::Payload &payload,
                                         ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AW_VALID:
    aw_queue_in.push_back(&payload);
    return TLM_ACCEPTED;
  case ARM::AXI::W_VALID:
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  case ARM::AXI::W_VALID_LAST: {
    w_queue_in.push_back(&payload);
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  }
  case ARM::AXI::B_READY:
    b_state = b_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unexpected phase: "
                           << get_axi_phase_string(phase));
    return TLM_ACCEPTED;
  }
}

tlm_sync_enum DMAEngine::nb_transport_bw(ARM::AXI::Payload &payload,
                                         ARM::AXI::Phase &phase) {
  auto it = payload_owner_map.find(&payload);
  if (it == payload_owner_map.end()) {
    SC_LOG_ERROR(this, "DMA Management: Unrecognized payload");
    return TLM_ACCEPTED;
  }

  switch (phase) {
  case ARM::AXI::AW_READY:
    aw_state = aw_state == REQ ? ACK : CLEAR;
    break;
  case ARM::AXI::W_READY:
    w_state = w_state == REQ ? ACK : CLEAR;
    break;
  case ARM::AXI::AR_READY:
    ar_state = ar_state == REQ ? ACK : CLEAR;
    break;
  default:
    break;
  }

  int vm_id = it->second;

  if (vm_id == internal_vm_id) {
    switch (phase) {
    case ARM::AXI::R_VALID:
    case ARM::AXI::R_VALID_LAST: {
      if (state == DMAEngineState::ReadFetch) {
        state = DMAEngineState::WriteFetch;
        fetch_write_payload = issue_fetch_write(current_request);
      }
      std::vector<uint8_t> buffer(payload.get_beat_data_length());
      payload.read_out_beat(r_beat_count, buffer.data());
      r_beat_count++;
      if (r_beat_count == payload.get_beat_count()) {
        r_beat_count = 0;
        payload_owner_map.erase(it);
      }
      fetch_write_payload->write_in_beat(buffer.data());
      w_queue_out.push_back(fetch_write_payload);
      phase = ARM::AXI::R_READY;
      return TLM_UPDATED;
    }
    case ARM::AXI::B_VALID:
      payload_owner_map.erase(it);
      state = DMAEngineState::Idle;
      phase = ARM::AXI::B_READY;
      return TLM_UPDATED;
    default:
      return TLM_ACCEPTED;
    }
  } else {
    // Backward to virtual initiator
    ARM::AXI::Phase prev_phase = phase;
    tlm_sync_enum reply = owners[vm_id]->nb_transport_bw_axi(payload, phase);

    // Reset if response sent
    if (is_r_valid_last(prev_phase)) {
      payload_owner_map.erase(it);
      state = DMAEngineState::Idle;
    }
    if (is_b_valid(prev_phase)) {
      payload_owner_map.erase(it);
      state = DMAEngineState::Idle;
    }

    return reply;
  }
}

// -------------------------------------------------------
// Helper Functions
// -------------------------------------------------------
ARM::AXI::Payload *DMAEngine::issue_fetch_read(const DMARequest &req) {
  unsigned axi_bytes = axi_width / 8;
  unsigned beats = (req.data_length + axi_bytes - 1) / axi_bytes;
  uint8_t len = (beats > 0) ? (beats - 1) : 0;
  ARM::AXI::Size size = get_axi_size(axi_width);

  ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
      ARM::AXI::COMMAND_READ, req.request_addr, size, len, req.burst);

  UserSignals user;
  user.core = req.core_id;
  user.src_chiplet = req.src_chiplet;
  user.src_module = req.src_module;
  user.dst_chiplet = req.dst_chiplet;
  user.dst_module = req.dst_module;
  user.fixed_address = true;

  payload->id = req.request_id;
  payload->user = user.encode();
  payload->cache = req.is_volatile ? ARM::AXI::CACHE_AR_DEVICE_NB
                                   : ARM::AXI::CACHE_AR_WRITE_THROUGH_RWA;

  // Mark ownership so DMA can handle responses internally
  payload_owner_map[payload] = internal_vm_id;
  ar_queue_out.push_back(payload);

  return payload;
}

ARM::AXI::Payload *DMAEngine::issue_fetch_write(const DMARequest &req) {
  unsigned axi_bytes = axi_width / 8;
  unsigned beats = (req.data_length + axi_bytes - 1) / axi_bytes;
  uint8_t len = (beats > 0) ? (beats - 1) : 0;
  ARM::AXI::Size size = get_axi_size(axi_width);

  ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
      ARM::AXI::COMMAND_WRITE, req.target_addr, size, len, req.burst);

  UserSignals user;
  user.core = req.core_id;
  user.src_chiplet = req.src_chiplet;
  user.src_module = req.target_module;
  user.dst_chiplet = req.src_chiplet;
  user.dst_module = req.target_module;
  user.fixed_address = true;

  payload->id = req.request_id;
  payload->user = user.encode();
  payload->cache = req.is_volatile ? ARM::AXI::CACHE_AR_DEVICE_NB
                                   : ARM::AXI::CACHE_AR_WRITE_THROUGH_RWA;

  // Mark ownership so DMA can handle responses internally
  payload_owner_map[payload] = internal_vm_id;
  aw_queue_out.push_back(payload);

  return payload;
}