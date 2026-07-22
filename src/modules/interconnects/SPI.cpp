#include "modules/interconnects/SPI.h"

#include "logging.h"

#include "common/Router.h"

SPI::SPI(sc_module_name name, unsigned chiplet_id, ChipletConfig chiplet_config,
         unsigned interconnect_id, InterconnectConfig interconnect_config,
         DMAEngine *dma_engine)
    : InterconnectBase(chiplet_id, chiplet_config, interconnect_id,
                       interconnect_config),
      sc_module(name), dma_engine(dma_engine),
      axi_in("axi_in", *this, &SPI::nb_transport_fw_axi,
             ARM::TLM::PROTOCOL_AXI4, axi_width),
      axi_out("axi_out", *this, &SPI::nb_transport_bw_axi,
              ARM::TLM::PROTOCOL_AXI4, axi_width) {
  if (dma_engine)
    dma_vm_id = dma_engine->register_virtual_initiator(this);

  links_in = new simple_target_socket_tagged<SPI>[num_links];
  links_out = new simple_initiator_socket_tagged<SPI>[num_links];

  for (int i = 0; i < num_links; ++i) {
    links_in[i].register_nb_transport_fw(this, &SPI::nb_transport_fw_link, i);
    links_out[i].register_nb_transport_bw(this, &SPI::nb_transport_bw_link, i);
  }

  irq_sockets = new simple_initiator_socket_tagged<SPI>[num_cores];

  // Register ports in InterconnectBase (implicit upcast to the TLM base socket
  // types; no reinterpret_cast, see InterconnectBase.h).
  axi_in_port = &axi_in;
  axi_out_port = &axi_out;
  for (unsigned i = 0; i < num_links; ++i) {
    link_in_ports[i] = &links_in[i];
    link_out_ports[i] = &links_out[i];
  }
  for (unsigned i = 0; i < num_cores; ++i)
    irq_ports[i] = &irq_sockets[i];

  active_links.resize(num_links, false);

  stats.register_utilization(this->name());

  SC_METHOD(clk_posedge);
  sensitive << clk.pos();
  dont_initialize();
}

SPI::~SPI() {
  delete[] links_in;
  delete[] links_out;
  delete[] irq_sockets;
}

void SPI::end_of_simulation() {
  for (size_t id = 0; id < connections.size(); ++id) {
    ConnectionConfig connection = connections[id];
    YAML::Node config = connection.node;

    stats.set_value(this->name(),
                    "transmission_size_bits_link" + std::to_string(id),
                    axi_width);
    stats.set_value(this->name(), "efficiency_pJ_bit_link" + std::to_string(id),
                    config["efficiency"].as<double>());
  }
}

void SPI::bind_clocks(Clocks &clocks) { clk.bind(clocks.get()); }

void SPI::clk_posedge() {
  clear_axi_states();

  // Bidirectional arbitration logic:
  // 1. If an AXI beat can be transmitted to the bus, send it.
  // 2. For AXI beats not the bus, arbitrate in the following order:
  //    - Link transfers (beats that must be forwarded)
  //    - Incoming AXI beats in this order: R, B, AW, AR, W

  // Check links for a beat destined for this chiplet (delivered to the local
  // bus). Scan the whole queue and remove only the first matching request,
  // preserving the FIFO order of the rest. (Previously this used
  // pop_front/push_front, which re-examined the head every iteration and never
  // looked past a non-local head -> permanent head-of-line blocking.)
  link_req_out = {-1, nullptr};
  for (size_t i = 0; i < links_queue.size(); ++i) {
    const LinkRequest &req = links_queue[i];

    uint8_t destination_id = UserSignals::decode(req.payload->user).dst_chiplet;
    int link_id = Router::instance().get_link_id(chiplet_id, interconnect_id,
                                                 destination_id);

    if (link_id == -1) {
      link_req_out = req;
      links_queue.erase(links_queue.begin() + i);
      break;
    }
  }
  // Put AXI beat in correct queue
  if (link_req_out.payload) {
    if (is_response(*link_req_out.payload)) {
      switch (link_req_out.payload->get_command()) {
      case ARM::AXI4::COMMAND_WRITE:
        b_queue_out.push_back(link_req_out);
        break;
      case ARM::AXI4::COMMAND_READ:
        r_queue_out.push_back(link_req_out);
        break;
      default:
        break;
      }
    } else {
      switch (link_req_out.payload->get_command()) {
      case ARM::AXI4::COMMAND_WRITE:
        if (get_beat_count(*link_req_out.payload) == -1)
          aw_queue_out.push_back(link_req_out);
        else
          w_queue_out.push_back(link_req_out);
        break;
      case ARM::AXI4::COMMAND_READ:
        ar_queue_out.push_back(link_req_out);
        break;
      default:
        break;
      }
    }
  }

  // Check links for a beat that must be forwarded (destination is not this
  // chiplet). Scan in FIFO order for the first forwardable request. If it can
  // be sent now, remove it; otherwise leave it queued in place for retry next
  // cycle. Stop at the first forwardable request either way.
  if (!active_transfer) {
    for (size_t i = 0; i < links_queue.size(); ++i) {
      const LinkRequest req = links_queue[i];

      uint8_t destination_id =
          UserSignals::decode(req.payload->user).dst_chiplet;

      if (destination_id != chiplet_id) {
        active_transfer = send_link_request(*req.payload);
        if (active_transfer) {
          active_links[req.link_id] = false;
          links_queue.erase(links_queue.begin() + i);
        }
        break;
      }
    }
  }
  // Check AXI channels for forwarding beats
  // Order: R, B, AW, AR, W
  if (!active_transfer) {
    if (!r_queue_in.empty()) {
      ARM::AXI::Payload *payload = r_queue_in.front();
      // Source becomes destination
      UserSignals user = UserSignals::decode(payload->user);
      user.dst_chiplet = user.src_chiplet;
      payload->user = user.encode();
      active_transfer = send_link_request(*payload);
      if (active_transfer) {
        r_queue_in.pop_front();
        ARM::AXI::Phase phase = ARM::AXI::R_READY;
        axi_out.nb_transport_fw(*payload, phase);
      }
    }
    if (!b_queue_in.empty() && !active_transfer) {
      ARM::AXI::Payload *payload = b_queue_in.front();
      // Source becomes destination
      UserSignals user = UserSignals::decode(payload->user);
      user.dst_chiplet = user.src_chiplet;
      payload->user = user.encode();
      active_transfer = send_link_request(*payload);
      if (active_transfer) {
        b_queue_in.pop_front();
        // We also send the IRQ here
        send_irq(*payload);
        ARM::AXI::Phase phase = ARM::AXI::B_READY;
        axi_out.nb_transport_fw(*payload, phase);
      }
    }
    if (!aw_queue_in.empty() && !active_transfer) {
      ARM::AXI::Payload *payload = aw_queue_in.front();
      active_transfer = send_link_request(*payload);
      if (active_transfer) {
        aw_queue_in.pop_front();
        register_payload_in(*payload);
        ARM::AXI::Phase phase = ARM::AXI::AW_READY;
        axi_in.nb_transport_bw(*payload, phase);
      }
    }
    if (!ar_queue_in.empty() && !active_transfer) {
      ARM::AXI::Payload *payload = ar_queue_in.front();
      active_transfer = send_link_request(*payload);
      if (active_transfer) {
        ar_queue_in.pop_front();
        register_payload_in(*payload);
        ARM::AXI::Phase phase = ARM::AXI::AR_READY;
        axi_in.nb_transport_bw(*payload, phase);
      }
    }
    if (!w_queue_in.empty() && !active_transfer) {
      ARM::AXI::Payload *payload = w_queue_in.front();
      active_transfer = send_link_request(*payload);
      if (active_transfer) {
        w_queue_in.pop_front();
        ARM::AXI::Phase phase = ARM::AXI::W_READY;
        axi_in.nb_transport_bw(*payload, phase);
      }
    }
  }

  send_axi_beats();
}

// -------------------------------------------------------
// Transport Functions
// -------------------------------------------------------
tlm_sync_enum SPI::nb_transport_fw_axi(ARM::AXI::Payload &payload,
                                       ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AW_VALID:
    aw_queue_in.push_back(&payload);
    break;
  case ARM::AXI::W_VALID:
  case ARM::AXI::W_VALID_LAST:
    w_queue_in.push_back(&payload);
    break;
  case ARM::AXI::B_READY:
    b_state = b_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::AR_VALID:
    ar_queue_in.push_back(&payload);
    break;
  case ARM::AXI::R_READY:
    r_state = r_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unexpected phase: "
                           << get_axi_phase_string(phase));
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum SPI::nb_transport_bw_axi(ARM::AXI::Payload &payload,
                                       ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AW_READY:
    aw_state = aw_state == REQ ? ACK : CLEAR;
    break;
  case ARM::AXI::W_READY:
    w_state = w_state == REQ ? ACK : CLEAR;
    break;
  case ARM::AXI::B_VALID:
    b_queue_in.push_back(&payload);
    break;
  case ARM::AXI::AR_READY:
    ar_state = ar_state == REQ ? ACK : CLEAR;
    break;
  case ARM::AXI::R_VALID:
  case ARM::AXI::R_VALID_LAST:
    r_queue_in.push_back(&payload);
    break;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unexpected phase: "
                           << get_axi_phase_string(phase));
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum SPI::nb_transport_fw_link(int id,
                                        tlm_generic_payload &transaction,
                                        tlm_phase &phase, sc_time &delay) {
  switch (phase) {
  case BEGIN_REQ:
    if (active_links[id])
      return TLM_ACCEPTED;

    active_links[id] = true;

    delay += delays.transfer_delay(id, transaction);

    sc_spawn([this, id, &transaction, delay]() {
      stats.set_active(this->name());
      wait(delay);
      stats.set_idle(this->name());
      stats.increment_counter(this->name(), "transmission_count_in_link" +
                                                std::to_string(id));

      ARM::AXI::Payload *payload =
          reinterpret_cast<ARM::AXI::Payload *>(transaction.get_data_ptr());
      links_queue.push_back({id, payload});
      tlm_phase resp_phase = BEGIN_RESP;
      sc_time resp_delay = SC_ZERO_TIME;
      links_in[id]->nb_transport_bw(transaction, resp_phase, resp_delay);
    });

    phase = END_REQ;
    return TLM_UPDATED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum SPI::nb_transport_bw_link(int id,
                                        tlm_generic_payload &transaction,
                                        tlm_phase &phase, sc_time &delay) {
  switch (phase) {
  case BEGIN_RESP:
    stats.set_idle(this->name());
    active_transfer = false;
    delete &transaction;

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

// -------------------------------------------------------
// Helper Functions
// -------------------------------------------------------
void SPI::clear_axi_states() {
  // AW channel
  if (aw_state == ACK) {
    aw_state = CLEAR;
    ARM::AXI::Payload *payload = aw_queue_out.front().payload;
    aw_queue_out.pop_front();
  }

  // W channel
  if (w_state == ACK) {
    w_state = CLEAR;
    ARM::AXI::Payload *payload = w_queue_out.front().payload;
    increment_beat_count(*payload);
    if (get_beat_count(*payload) == payload->get_beat_count())
      unregister_beat_count(*payload);
    w_queue_out.pop_front();
  }

  // B channel
  if (b_state == ACK) {
    b_state = CLEAR;
    ARM::AXI::Payload *payload = b_queue_out.front().payload;
    unregister_payload_in(*payload);
    b_queue_out.pop_front();
  }

  // AR channel
  if (ar_state == ACK) {
    ar_state = CLEAR;
    ar_queue_out.pop_front();
  }

  // R channel
  if (r_state == ACK) {
    r_state = CLEAR;
    ARM::AXI::Payload *payload = r_queue_out.front().payload;
    increment_beat_count(*payload);
    if (get_beat_count(*payload) == payload->get_beat_count())
      unregister_beat_count(*payload);
    r_queue_out.pop_front();
  }
}

void SPI::send_axi_beats() {
  // AW channel
  if (aw_state == CLEAR && !aw_queue_out.empty()) {
    aw_state = REQ;
    LinkRequest link_req = aw_queue_out.front();
    ARM::AXI::Payload *payload = link_req.payload;
    ARM::AXI::Phase phase = ARM::AXI::AW_VALID;
    register_beat_count(*payload);
    active_links[link_req.link_id] = false;
    bool use_dma = UserSignals::decode(payload->user).extension_mask == 0 &&
                   dma_vm_id != -1;
    if (use_dma && !send_dma_request(*payload, phase)) {
      aw_state = CLEAR;
      active_links[link_req.link_id] = true;
    } else if (!use_dma) {
      tlm_sync_enum reply = axi_out.nb_transport_fw(*payload, phase);
      if (reply == TLM_UPDATED) {
        SC_LOG_ASSERT(this, phase == ARM::AXI::AW_READY,
                      "AXI TLM Protocol: Unexpected phase");
        aw_state = ACK;
      }
    }
  }

  // W channel
  if (w_state == CLEAR && !w_queue_out.empty()) {
    w_state = REQ;
    LinkRequest link_req = w_queue_out.front();
    ARM::AXI::Payload *payload = link_req.payload;
    ARM::AXI::Phase phase =
        (get_beat_count(*payload) + 1 == payload->get_beat_count())
            ? ARM::AXI::W_VALID_LAST
            : ARM::AXI::W_VALID;
    bool use_dma = UserSignals::decode(payload->user).extension_mask == 0 &&
                   dma_vm_id != -1;
    active_links[link_req.link_id] = false;
    if (use_dma && !send_dma_request(*payload, phase)) {
      w_state = CLEAR;
      active_links[link_req.link_id] = true;
    } else if (!use_dma) {
      tlm_sync_enum reply = axi_out.nb_transport_fw(*payload, phase);
      if (reply == TLM_UPDATED) {
        SC_LOG_ASSERT(this, phase == ARM::AXI::W_READY,
                      "AXI TLM Protocol: Unexpected phase");
        w_state = ACK;
      }
    }
  }

  // B channel
  if (b_state == CLEAR && !b_queue_out.empty()) {
    b_state = REQ;
    LinkRequest link_req = b_queue_out.front();
    ARM::AXI::Payload *payload = link_req.payload;
    ARM::AXI::Phase phase = ARM::AXI::B_VALID;
    active_links[link_req.link_id] = false;
    tlm_sync_enum reply = axi_in.nb_transport_bw(*payload, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::B_READY,
                    "AXI TLM Protocol: Unexpected phase");
      b_state = ACK;
    }
  }

  // AR channel
  if (ar_state == CLEAR && !ar_queue_out.empty()) {
    ar_state = REQ;
    LinkRequest link_req = ar_queue_out.front();
    ARM::AXI::Payload *payload = link_req.payload;
    ARM::AXI::Phase phase = ARM::AXI::AR_VALID;
    bool use_dma = UserSignals::decode(payload->user).extension_mask == 0 &&
                   dma_vm_id != -1;
    active_links[link_req.link_id] = false;
    if (use_dma && !send_dma_request(*payload, phase)) {
      ar_state = CLEAR;
      active_links[link_req.link_id] = true;
    } else if (!use_dma) {
      tlm_sync_enum reply = axi_out.nb_transport_fw(*payload, phase);
      if (reply == TLM_UPDATED) {
        SC_LOG_ASSERT(this, phase == ARM::AXI::AR_READY,
                      "AXI TLM Protocol: Unexpected phase");
        ar_state = ACK;
      }
    }
  }

  // R channel
  if (r_state == CLEAR && !r_queue_out.empty()) {
    r_state = REQ;
    LinkRequest link_req = r_queue_out.front();
    ARM::AXI::Payload *payload = link_req.payload;
    register_beat_count(*payload);
    ARM::AXI::Phase phase =
        (get_beat_count(*payload) + 1 == payload->get_beat_count())
            ? ARM::AXI::R_VALID_LAST
            : ARM::AXI::R_VALID;
    active_links[link_req.link_id] = false;
    tlm_sync_enum reply = axi_in.nb_transport_bw(*payload, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::R_READY,
                    "AXI TLM Protocol: Unexpected phase");
      r_state = ACK;
    }
  }
}

void SPI::send_irq(ARM::AXI::Payload &payload) {
  // Don't send an IRQ if:
  // - there are no cores
  // - DMA engine is used. It will send it.
  // - extension layer is used. It will send it.
  if (num_cores == 0 || dma_vm_id != -1 ||
      UserSignals::decode(payload.user).extension_mask != 0)
    return;

  UserSignals user = UserSignals::decode(payload.user);

  auto *irq = new IRQ();
  irq->request_id = payload.id;
  irq->target_module = user.dst_module;
  irq->target_address = payload.get_address();
  irq->burst = payload.get_burst();
  irq->data_length = payload.get_data_length();

  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;

  tlm_generic_payload *transaction = new tlm_generic_payload;
  transaction->set_data_ptr(reinterpret_cast<unsigned char *>(irq));
  transaction->set_data_length(sizeof(IRQ));
  transaction->set_command(TLM_WRITE_COMMAND);

  // Deliver the completion IRQ to the originating core, not always core 0.
  const unsigned irq_core = user.core < num_cores ? user.core : 0;
  SC_LOG_DEBUG(this, "Sending IRQ to core " << irq_core);
  irq_sockets[irq_core]->nb_transport_fw(*transaction, phase, delay);
}

bool SPI::send_link_request(ARM::AXI::Payload &payload) {
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;

  tlm_generic_payload *transaction = new tlm_generic_payload;

  transaction->set_data_ptr(reinterpret_cast<unsigned char *>(&payload));

  uint8_t destination_id = UserSignals::decode(payload.user).dst_chiplet;
  int link_id = Router::instance().get_link_id(chiplet_id, interconnect_id,
                                               destination_id);
  if (link_id == -1)
    SC_LOG_ERROR(this, "No valid routing path from " << chiplet_id << " to "
                                                     << int(destination_id));

  tlm_sync_enum reply =
      links_out[link_id]->nb_transport_fw(*transaction, phase, delay);

  if (reply == TLM_UPDATED) {
    stats.set_active(this->name());
    stats.increment_counter(this->name(), "transmission_count_out_link" +
                                              std::to_string(link_id));
    stats.update_accum(this->name(),
                       "transmission_duration_out_us_link" +
                           std::to_string(link_id),
                       delay.to_seconds() * 1e6);
    return true;
  } else
    return false;
}

void SPI::register_payload_in(ARM::AXI::Payload &payload) {
  auto it = payloads_in.find(&payload);
  if (it == payloads_in.end())
    payloads_in[&payload] = true;
}

void SPI::unregister_payload_in(ARM::AXI::Payload &payload) {
  auto it = payloads_in.find(&payload);
  if (it != payloads_in.end())
    payloads_in.erase(&payload);
  else
    SC_LOG_ERROR(this, "SPI Implementation Error");
}

bool SPI::is_response(ARM::AXI::Payload &payload) {
  auto it = payloads_in.find(&payload);
  if (it != payloads_in.end())
    return true;
  else
    return false;
}

void SPI::register_beat_count(ARM::AXI::Payload &payload) {
  auto it = payload_beats.find(&payload);
  if (it == payload_beats.end())
    payload_beats[&payload] = 0;
}

void SPI::unregister_beat_count(ARM::AXI::Payload &payload) {
  auto it = payload_beats.find(&payload);
  if (it != payload_beats.end())
    payload_beats.erase(&payload);
  else
    SC_LOG_ERROR(this, "SPI Implementation Error");
}

void SPI::increment_beat_count(ARM::AXI::Payload &payload) {
  auto it = payload_beats.find(&payload);
  if (it != payload_beats.end())
    it->second += 1;
  else
    SC_LOG_ERROR(this, "SPI Implementation Error");
}

int SPI::get_beat_count(ARM::AXI::Payload &payload) {
  auto it = payload_beats.find(&payload);
  if (it != payload_beats.end())
    return it->second;
  else
    return -1;
}