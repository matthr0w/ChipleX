#include "modules/DMAEngine.h"

#include "logging.h"

DMAEngine::DMAEngine(sc_module_name name, YAML::Node config)
    : sc_module(name), axi_width(config["axi"]["width"].as<unsigned>()),
      tsocket("tsocket", *this, &DMAEngine::nb_transport_bw,
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

int DMAEngine::register_virtual_initiator(VirtualAXIInitiatorIF *owner) {
  int vm_id = static_cast<int>(owners.size());
  owners.push_back(owner);
  return vm_id;
}

void DMAEngine::unregister_virtual_initiator(int vm_id) {
  if (vm_id < 0 || static_cast<size_t>(vm_id) >= owners.size())
    return;
  owners[vm_id] = nullptr;
}

void DMAEngine::clk_posedge() {
  // AW channel
  if (aw_state == ACK) {
    aw_state = CLEAR;
    aw_queue.pop_front();
  }

  // W channel
  if (w_state == ACK) {
    w_state = CLEAR;
    w_beat_count++;
    if (w_beat_count == w_queue.front()->get_beat_count())
      w_beat_count = 0;
    w_queue.pop_front();
  }

  // AR channel
  if (ar_state == ACK) {
    ar_state = CLEAR;
    ar_queue.pop_front();
  }
}

void DMAEngine::clk_negedge() {
  // AW channel
  if (aw_state == CLEAR && !aw_queue.empty()) {
    aw_state = REQ;
    ARM::AXI::Payload *payload = aw_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::AW_VALID;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::AW_READY,
                    "AXI TLM Protocol: Unexpected phase");
      aw_state = ACK;
      // Backward to virtual initiator
      int vm_id = payload_owner_map.find(payload)->second;
      owners[vm_id]->nb_transport_bw_axi(*payload, phase);
    }
  }

  // W channel
  if (w_state == CLEAR && !w_queue.empty()) {
    w_state = REQ;
    ARM::AXI::Payload *payload = w_queue.front();
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
      owners[vm_id]->nb_transport_bw_axi(*payload, phase);
    }
  }

  // AR channel
  if (ar_state == CLEAR && !ar_queue.empty()) {
    ar_state = REQ;
    ARM::AXI::Payload *payload = ar_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::AR_VALID;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::AR_READY,
                    "AXI TLM Protocol: Unexpected phase");
      ar_state = ACK;
      // Backward to virtual initiator
      int vm_id = payload_owner_map.find(payload)->second;
      owners[vm_id]->nb_transport_bw_axi(*payload, phase);
    }
  }
}

bool DMAEngine::forward_from_virtual(int vm_id, ARM::AXI::Payload &payload,
                                     ARM::AXI::Channel channel) {
  SC_LOG_ASSERT(this, vm_id >= 0 && static_cast<size_t>(vm_id) < owners.size(),
                "DMA Registration: Unregistered virtual");
  VirtualAXIInitiatorIF *owner = owners[vm_id];
  SC_LOG_ASSERT(this, owner != nullptr,
                "DMA Registration: Unregistered virtual");

  payload_owner_map[&payload] = vm_id;

  switch (channel) {
  case ARM::AXI::Channel::CHANNEL_AW:
    aw_queue.push_back(&payload);
    return true;
  case ARM::AXI::Channel::CHANNEL_W:
    w_queue.push_back(&payload);
    return true;
  case ARM::AXI::Channel::CHANNEL_AR:
    ar_queue.push_back(&payload);
    return true;
  default:
    return false;
  }
}

// -------------------------------------------------------
// Transport Functions
// -------------------------------------------------------
tlm_sync_enum DMAEngine::nb_transport_bw(ARM::AXI::Payload &payload,
                                         ARM::AXI::Phase &phase) {
  auto it = payload_owner_map.find(&payload);
  if (it == payload_owner_map.end()) {
    SC_LOG_ERROR(this, "Unrecognized payload");
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

  // Backward to virtual initiator
  int vm_id = it->second;
  ARM::AXI::Phase prev_phase = phase;
  tlm_sync_enum reply = owners[vm_id]->nb_transport_bw_axi(payload, phase);

  // Remove payload if response sent
  if (is_r_valid_last(prev_phase) && is_r_ready(phase))
    payload_owner_map.erase(it);
  if (is_b_valid(prev_phase) && is_b_ready(phase))
    payload_owner_map.erase(it);

  return reply;
}