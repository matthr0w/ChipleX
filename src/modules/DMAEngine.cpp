#include "modules/DMAEngine.h"

DMAEngine::DMAEngine(sc_module_name name, unsigned int axi_width)
    : sc_module(name), axi_width(axi_width),
      isocket("isocket", *this, &DMAEngine::nb_transport_bw,
              ARM::TLM::PROTOCOL_AXI4, axi_width) {
  SC_METHOD(clock_posedge);
  sensitive << clock.pos();
  dont_initialize();

  SC_METHOD(clock_negedge);
  sensitive << clock.neg();
  dont_initialize();
}

int DMAEngine::register_virtual_initiator(VirtualAXIInitiatorIF *owner) {
  int id = static_cast<int>(owners_.size());
  owners_.push_back(owner);
  return id;
}

void DMAEngine::unregister_virtual_initiator(int vm_id) {
  if (vm_id < 0 || static_cast<size_t>(vm_id) >= owners_.size())
    return;
  owners_[vm_id] = nullptr;
}

void DMAEngine::clock_posedge() {
  if (ar_state == ACK)
    ar_state = CLEAR;

  if (aw_state == ACK)
    aw_state = CLEAR;

  if (w_state == ACK)
    w_state = CLEAR;
}

void DMAEngine::clock_negedge() {
  /* Send next payload ARVALID */
  if ((ar_state == CLEAR || ar_state == REQ) && !ar_queue.empty()) {
    ARM::AXI::Payload *payload = ar_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::AR_VALID;

    ar_state = REQ;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::AR_READY);
      ar_state = ACK;
      ar_queue.pop_front();
    }
  }

  /* Send next payload AWVALID */
  if ((aw_state == CLEAR || aw_state == REQ) && !aw_queue.empty()) {
    ARM::AXI::Payload *payload = aw_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::AW_VALID;

    aw_state = REQ;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::AW_READY);
      aw_state = ACK;
      aw_queue.pop_front();
      w_queue.push_back(payload);
    }
  }

  /* Send write beat WVALID */
  if ((w_state == CLEAR || w_state == REQ) && !w_queue.empty()) {
    ARM::AXI::Payload *payload = w_queue.front();
    ARM::AXI::Phase phase = (w_beat_count + 1 == payload->get_beat_count())
                                ? ARM::AXI::W_VALID_LAST
                                : ARM::AXI::W_VALID;

    w_state = REQ;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::W_READY);
      w_state = ACK;
      w_beat_count++;
      if (w_beat_count == payload->get_beat_count()) {
        w_queue.pop_front();
        w_beat_count = 0;
      }
    }
  }
}

bool DMAEngine::forward_from_virtual(int vm_id, ARM::AXI::Payload &payload) {
  assert(vm_id >= 0 && static_cast<size_t>(vm_id) < owners_.size());
  VirtualAXIInitiatorIF *owner = owners_[vm_id];
  assert(owner != nullptr);

  payload_owner_map_.emplace(&payload, vm_id);

  switch (payload.get_command()) {
  case ARM::AXI::COMMAND_WRITE:
    aw_queue.push_back(&payload);
    return true;
  case ARM::AXI::COMMAND_READ:
    ar_queue.push_back(&payload);
    return true;
  default:
    return false;
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum DMAEngine::nb_transport_bw(ARM::AXI::Payload &payload,
                                         ARM::AXI::Phase &phase) {
  // Find which virtual initiator issued this payload
  auto it = payload_owner_map_.find(&payload);
  if (it == payload_owner_map_.end()) {
    SC_REPORT_ERROR(name(), "DMA Engine: Unrecognized payload");
    return TLM_ACCEPTED;
  }

  switch (phase) {
  case ARM::AXI::AR_READY:
    ar_state = ACK;
    break;
  case ARM::AXI::AW_READY:
    aw_state = ACK;
    break;
  case ARM::AXI::W_READY:
    w_state = ACK;
    break;
  default:
    break;
  }

  // Backward to virtual initiator
  int vm_id = it->second;
  ARM::AXI::Phase prev_phase = phase;
  tlm_sync_enum reply = owners_[vm_id]->nb_transport_bw_axi(payload, phase);

  // Remove payload if response sent
  if (is_r_valid_last(prev_phase) && is_r_ready(phase)) {
    payload_owner_map_.erase(it);
  }
  if (is_b_valid(prev_phase) && is_b_ready(phase)) {
    payload_owner_map_.erase(it);
  }

  return reply;
}