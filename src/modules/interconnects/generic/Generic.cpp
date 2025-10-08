#include "modules/interconnects/generic/Generic.h"

#include "logging.h"

#include "common/Router.h"

GenericInterconnect::GenericInterconnect(sc_module_name name,
                                         unsigned chiplet_id,
                                         ChipletConfig chiplet_config,
                                         InterconnectConfig interconnect_config,
                                         unsigned num_cores,
                                         DMAEngine *dma_engine)
    : InterconnectBase(num_cores, chiplet_config.connections.size()),
      sc_module(name), chiplet_id(chiplet_id), num_cores(num_cores),
      num_links(chiplet_config.connections.size()),
      axi_width(chiplet_config.config["axi"]["width"].as<unsigned>()),
      flit_size(
          interconnect_config.config["protocol"]["flit_size"].as<unsigned>()),
      overhead_size(interconnect_config.config["protocol"]["overhead_size"]
                        .as<unsigned>()),
      staging_buffer_size(
          interconnect_config.config["protocol"]["staging_buffer_size"]
              .as<unsigned>()),
      link_buffer_size(
          interconnect_config.config["phy"]["link_buffer_size"].as<unsigned>()),
      connections(chiplet_config.connections), dma_engine(dma_engine),
      axi_in("axi_in", *this, &GenericInterconnect::nb_transport_fw_axi,
             ARM::TLM::PROTOCOL_AXI4, axi_width),
      axi_out("axi_out", *this, &GenericInterconnect::nb_transport_bw_axi,
              ARM::TLM::PROTOCOL_AXI4, axi_width) {
  // Assertions
  sc_assert(flit_size >= overhead_size + Flit::header_size() + axi_width / 8);

  dma_vm_id = dma_engine->register_virtual_initiator(this);

  phy_in = new simple_target_socket_tagged<GenericInterconnect>[num_links];
  phy_out = new simple_initiator_socket_tagged<GenericInterconnect>[num_links];

  for (int i = 0; i < num_links; ++i) {
    phy_in[i].register_nb_transport_fw(
        this, &GenericInterconnect::nb_transport_fw_phy, i);
    phy_out[i].register_nb_transport_bw(
        this, &GenericInterconnect::nb_transport_bw_phy, i);
  }

  irq_sockets =
      new simple_initiator_socket_tagged<GenericInterconnect>[num_cores];

  // Register ports in InterconnectBase
  axi_in_port =
      reinterpret_cast<ARM::AXI::SimpleTargetSocket<InterconnectBase> *>(
          &axi_in);
  axi_out_port =
      reinterpret_cast<ARM::AXI::SimpleInitiatorSocket<InterconnectBase> *>(
          &axi_out);
  for (int i = 0; i < num_links; ++i) {
    link_in_ports[i] =
        reinterpret_cast<simple_target_socket_tagged<InterconnectBase> *>(
            &phy_in[i]);
    link_out_ports[i] =
        reinterpret_cast<simple_initiator_socket_tagged<InterconnectBase> *>(
            &phy_out[i]);
  }
  for (int i = 0; i < num_cores; ++i)
    irq_ports[i] =
        reinterpret_cast<simple_initiator_socket_tagged<InterconnectBase> *>(
            &irq_sockets[i]);

  staging_buffer.resize(staging_buffer_size, 0);

  tx_buffers.resize(num_links, std::vector<uint8_t>(link_buffer_size, 0));
  rx_buffers.resize(num_links, std::vector<uint8_t>(link_buffer_size, 0));

  tx_ptrs.resize(tx_buffers.size(), 0);
  rx_ptrs.resize(rx_buffers.size(), 0);

  phy_active_tx.resize(tx_buffers.size(), false);

  flit_header_bytes = overhead_size + Flit::header_size();
  flit_data_bytes = flit_size - flit_header_bytes;

  SC_METHOD(axi_clk_posedge);
  sensitive << axi_clk.pos();
  dont_initialize();

  SC_METHOD(protocol_clk_posedge);
  sensitive << protocol_clk.pos();
  dont_initialize();

  SC_METHOD(phy_clk_posedge);
  sensitive << phy_clk.pos();
  dont_initialize();
}

GenericInterconnect::~GenericInterconnect() {
  delete[] phy_in;
  delete[] phy_out;
  delete[] irq_sockets;
}

void GenericInterconnect::bind_clock(sc_clock &clk) {
  axi_clk.bind(clk);
  protocol_clk.bind(clk);
  phy_clk.bind(clk);
}

void GenericInterconnect::axi_clk_posedge() {
  clear_axi_states();
  handle_axi_channels();
  send_axi_beats();
}

void GenericInterconnect::protocol_clk_posedge() {
  // Process staging buffer
  if (staging_buffer_ptr > 0) {
    size_t flit_payload_bytes = 0;

    auto align_down = [&](size_t val) {
      const unsigned axi_width_bytes = axi_width / 8;
      return static_cast<size_t>(val / axi_width_bytes) * axi_width_bytes;
    };

    if (staging_buffer_ptr >= flit_data_bytes)
      flit_payload_bytes = align_down(flit_data_bytes);
    else if (flush_staging_buffer)
      flit_payload_bytes = align_down(staging_buffer_ptr);

    size_t flit_padding_bytes = flit_data_bytes - flit_payload_bytes;

    if (flit_payload_bytes > 0) {
      Flit flit(flit_data_bytes);

      // Fill flit payload
      std::fill(flit.axi_data.data.begin(), flit.axi_data.data.end(), 0);
      std::memcpy(flit.axi_data.data.data(), staging_buffer.data(),
                  flit_payload_bytes);

      // Fill header
      flit.axi_ch = axi_transaction.channel;
      flit.len = axi_transaction.payload->get_len();
      flit.burst = axi_transaction.payload->get_burst();
      flit.id = axi_transaction.payload->id;
      flit.user = axi_transaction.payload->user;

      // Determine Tx buffer
      uint8_t destination_id =
          UserSignals::decode(axi_transaction.payload->user).destination;
      const unsigned tx_idx =
          Router::instance().get_link_id(chiplet_id, destination_id);
      if (tx_idx == -1)
        SC_LOG_ERROR(this, "No valid routing path from "
                               << chiplet_id << " to " << int(destination_id));

      const size_t tail = tx_ptrs[tx_idx];
      const size_t room = tx_buffers[tx_idx].size() - tail;

      if (room >= flit_size) {
        write_flit_to_buffer(tx_buffers[tx_idx].data() + tail, flit,
                             flit_payload_bytes, flit_padding_bytes);
        tx_ptrs[tx_idx] += flit_size;

        // Consume from staging buffer
        std::memmove(staging_buffer.data(),
                     staging_buffer.data() + flit_payload_bytes,
                     staging_buffer_ptr - flit_payload_bytes);
        staging_buffer_ptr -= flit_payload_bytes;

        // Reset if done
        if (flush_staging_buffer && staging_buffer_ptr == 0)
          flush_staging_buffer = false;

        if (reset_axi_channel && staging_buffer_ptr == 0) {
          axi_transaction.payload = nullptr;
          axi_transaction.channel = None;
          axi_transaction.beat_idx = 0;
          reset_axi_channel = false;
        }
      }
    }
  }

  // Process Rx buffers
  for (size_t rx_idx = 0; rx_idx < rx_buffers.size(); ++rx_idx) {
    if (rx_ptrs[rx_idx] < flit_size)
      continue;

    Flit flit = read_flit_from_buffer(rx_buffers[rx_idx].data());

    UserSignals user = UserSignals::decode(flit.user);

    // Forward flit if not for this chiplet
    if (user.destination != chiplet_id) {
      forward_flit(rx_idx, user.destination);
      continue;
    }

    // Process local flit
    process_flit(rx_idx, flit);
  }
}

void GenericInterconnect::phy_clk_posedge() {
  // Send from Tx buffers
  for (size_t tx_idx = 0; tx_idx < tx_buffers.size(); ++tx_idx) {
    if (tx_ptrs[tx_idx] < flit_size || phy_active_tx[tx_idx])
      continue;

    // Construct flit payload
    auto *flit = new tlm_generic_payload;
    flit->set_data_ptr(tx_buffers[tx_idx].data());
    flit->set_data_length(flit_size);

    tlm_phase phase = BEGIN_REQ;
    sc_time delay = SC_ZERO_TIME;
    tlm_sync_enum reply = phy_out[tx_idx]->nb_transport_fw(*flit, phase, delay);

    if (reply == TLM_UPDATED)
      phy_active_tx[tx_idx] = true;
    else
      delete flit;
  }

  // Receive into Rx buffers
  while (!phy_queue.empty()) {
    PhyRequest request = phy_queue.front();
    phy_queue.pop_front();

    const int rx_idx = request.link_id;
    const size_t tail = rx_ptrs[rx_idx];
    const size_t room = rx_buffers[rx_idx].size() - tail;

    if (room >= flit_size) {
      std::memcpy(&rx_buffers[rx_idx][tail],
                  request.transaction->get_data_ptr(), flit_size);
      rx_ptrs[rx_idx] += flit_size;

      tlm_phase phase = BEGIN_RESP;
      sc_time delay = SC_ZERO_TIME;
      phy_in[rx_idx]->nb_transport_bw(*request.transaction, phase, delay);
    }
  }
}

// -------------------------------------------------------
// Transport Functions
// -------------------------------------------------------
tlm_sync_enum
GenericInterconnect::nb_transport_fw_axi(ARM::AXI::Payload &payload,
                                         ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AW_VALID:
    if (axi_transaction.channel != None ||
        staging_buffer_ptr + axi_width > staging_buffer_size)
      return TLM_ACCEPTED; // Backpressure
    flush_staging_buffer = true;
    axi_transaction.payload = &payload;
    axi_transaction.channel = AW;
    aw_queue_in.push_back(&payload);
    phase = ARM::AXI::AW_READY;
    return TLM_UPDATED;
  case ARM::AXI::W_VALID:
    if (staging_buffer_ptr + axi_width > staging_buffer_size)
      return TLM_ACCEPTED; // Backpressure
    axi_transaction.channel = W;
    w_queue_in.push_back(&payload);
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  case ARM::AXI::W_VALID_LAST:
    if (staging_buffer_ptr + axi_width > staging_buffer_size)
      return TLM_ACCEPTED; // Backpressure
    flush_staging_buffer = true;
    reset_axi_channel = true;
    axi_transaction.channel = W;
    w_queue_in.push_back(&payload);
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  case ARM::AXI::AR_VALID:
    if (axi_transaction.channel != None ||
        staging_buffer_ptr + axi_width > staging_buffer_size)
      return TLM_ACCEPTED; // Backpressure
    flush_staging_buffer = true;
    reset_axi_channel = true;
    axi_transaction.payload = &payload;
    axi_transaction.channel = AR;
    ar_queue_in.push_back(&payload);
    phase = ARM::AXI::AR_READY;
    return TLM_UPDATED;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unexpected phase");
    return TLM_ACCEPTED;
  }
}

tlm_sync_enum
GenericInterconnect::nb_transport_bw_axi(ARM::AXI::Payload &payload,
                                         ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AW_READY:
    aw_state = aw_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::W_READY:
    w_state = w_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::B_VALID:
    if (axi_transaction.channel != None ||
        staging_buffer_ptr + axi_width > staging_buffer_size)
      return TLM_ACCEPTED; // Backpressure
    flush_staging_buffer = true;
    reset_axi_channel = true;
    axi_transaction.payload = &payload;
    axi_transaction.channel = B;
    b_queue_in.push_back(&payload);
    phase = ARM::AXI::B_READY;
    return TLM_UPDATED;
  case ARM::AXI::AR_READY:
    ar_state = ar_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::R_VALID:
    if ((axi_transaction.channel != None && axi_transaction.channel != R) ||
        staging_buffer_ptr + axi_width > staging_buffer_size)
      return TLM_ACCEPTED; // Backpressure
    axi_transaction.payload = &payload;
    axi_transaction.channel = R;
    r_queue_in.push_back(&payload);
    phase = ARM::AXI::R_READY;
    return TLM_UPDATED;
  case ARM::AXI::R_VALID_LAST:
    if ((axi_transaction.channel != None && axi_transaction.channel != R) ||
        staging_buffer_ptr + axi_width > staging_buffer_size)
      return TLM_ACCEPTED; // Backpressure
    flush_staging_buffer = true;
    reset_axi_channel = true;
    axi_transaction.payload = &payload;
    axi_transaction.channel = R;
    r_queue_in.push_back(&payload);
    phase = ARM::AXI::R_READY;
    return TLM_UPDATED;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unrecognized phase");
    return TLM_ACCEPTED;
  }
}

tlm_sync_enum
GenericInterconnect::nb_transport_fw_phy(int id,
                                         tlm_generic_payload &transaction,
                                         tlm_phase &phase, sc_time &delay) {
  switch (phase) {
  case BEGIN_REQ:
    if (rx_ptrs[id] + flit_size > rx_buffers[id].size())
      return TLM_ACCEPTED; // Backpressure

    Transfer transfer = delays.transfer_delay(id, transaction);
    delay += transfer.delay;

    // Drop bad transfers
    if (transfer.success) {
      tlm_generic_payload *tptr = &transaction;
      sc_spawn([this, id, tptr, delay]() {
        wait(delay);
        phy_queue.push_back({id, tptr});
      });
    }

    phase = END_REQ;
    return TLM_UPDATED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum
GenericInterconnect::nb_transport_bw_phy(int id,
                                         tlm_generic_payload &transaction,
                                         tlm_phase &phase, sc_time &delay) {
  switch (phase) {
  case BEGIN_RESP:
    // Consume from Tx buffer
    std::memmove(tx_buffers[id].data(), tx_buffers[id].data() + flit_size,
                 tx_ptrs[id] - flit_size);
    tx_ptrs[id] -= flit_size;

    phy_active_tx[id] = false;
    delete &transaction;

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

// -------------------------------------------------------
// Helper Functions
// -------------------------------------------------------
void GenericInterconnect::clear_axi_states() {
  // AW channel
  if (aw_state == ACK) {
    aw_state = CLEAR;
    aw_queue_out.pop_front();
  }

  // W channel
  if (w_state == ACK) {
    w_state = CLEAR;
    w_beat_count++;
    if (w_beat_count == w_queue_out.front()->get_beat_count()) {
      w_beat_count = 0;
      flit_w_beat_count = 0;
      erase_payload(manager_payloads, w_queue_out.front());
      send_irq(*w_queue_out.front());
    }
    w_queue_out.pop_front();
  }

  // B channel
  if (b_state == ACK) {
    b_state = CLEAR;
    erase_payload(subordinate_payloads, b_queue_out.front());
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
    r_beat_count++;
    if (r_beat_count == r_queue_out.front()->get_beat_count()) {
      r_beat_count = 0;
      flit_r_beat_count = 0;
      erase_payload(subordinate_payloads, r_queue_out.front());
    }
    r_queue_out.pop_front();
  }
}

void GenericInterconnect::handle_axi_channels() {
  // AW channel
  if (!aw_queue_in.empty()) {
    auto *payload = aw_queue_in.front();
    aw_queue_in.pop_front();

    // Save payload for response
    UserSignals user = UserSignals::decode(payload->user);
    PayloadKey key = {payload->id, user.core, user.source};
    subordinate_payloads[key] = payload;

    // Write address to staging buffer
    if (staging_buffer.size() - staging_buffer_ptr >= sizeof(uint32_t)) {
      uint32_t address = payload->get_address();
      std::memcpy(&staging_buffer[staging_buffer_ptr], &address,
                  sizeof(uint32_t));
      staging_buffer_ptr += sizeof(uint32_t);
    }
  }

  // W channel
  if (!w_queue_in.empty()) {
    auto *payload = w_queue_in.front();
    w_queue_in.pop_front();

    // Write data to staging buffer
    size_t beat_bytes = payload->get_beat_data_length();
    if (staging_buffer.size() - staging_buffer_ptr >= beat_bytes) {
      payload->write_out_beat(axi_transaction.beat_idx,
                              &staging_buffer[staging_buffer_ptr]);
      axi_transaction.beat_idx += 1;
      staging_buffer_ptr += beat_bytes;
    }
  }

  // B channel
  if (!b_queue_in.empty()) {
    auto *payload = b_queue_in.front();
    b_queue_in.pop_front();

    // Source becomes destination
    UserSignals user = UserSignals::decode(axi_transaction.payload->user);
    user.destination = user.source;
    axi_transaction.payload->user = user.encode();

    // Write response to staging buffer
    if (staging_buffer.size() - staging_buffer_ptr >=
        sizeof(ARM::AXI4::RespEnum)) {
      ARM::AXI4::RespEnum resp = payload->get_resp();
      std::memcpy(&staging_buffer[staging_buffer_ptr], &resp,
                  sizeof(ARM::AXI4::RespEnum));
      staging_buffer_ptr += sizeof(ARM::AXI4::RespEnum);
    }
  }

  // AR channel
  if (!ar_queue_in.empty()) {
    auto *payload = ar_queue_in.front();
    ar_queue_in.pop_front();

    // Save payload for response
    UserSignals user = UserSignals::decode(payload->user);
    PayloadKey key = {payload->id, user.core, user.source};
    subordinate_payloads[key] = payload;

    // Write address to staging buffer
    if (staging_buffer.size() - staging_buffer_ptr >= sizeof(uint32_t)) {
      uint32_t address = payload->get_address();
      std::memcpy(&staging_buffer[staging_buffer_ptr], &address,
                  sizeof(uint32_t));
      staging_buffer_ptr += sizeof(uint32_t);
    }
  }

  // R channel
  if (!r_queue_in.empty()) {
    auto *payload = r_queue_in.front();
    r_queue_in.pop_front();

    // Source becomes destination
    UserSignals user = UserSignals::decode(axi_transaction.payload->user);
    user.destination = user.source;
    axi_transaction.payload->user = user.encode();

    // Write data to staging buffer
    size_t beat_bytes = payload->get_beat_data_length();
    if (staging_buffer.size() - staging_buffer_ptr >= beat_bytes) {
      payload->read_out_beat(axi_transaction.beat_idx,
                             &staging_buffer[staging_buffer_ptr]);
      axi_transaction.beat_idx += 1;
      staging_buffer_ptr += beat_bytes;
    }
  }
}

void GenericInterconnect::send_axi_beats() {
  // AW channel
  if (aw_state == CLEAR && !aw_queue_out.empty()) {
    ARM::AXI::Payload *payload = aw_queue_out.front();
    ARM::AXI::Phase phase = ARM::AXI::AW_VALID;

    aw_state = REQ;
    tlm_sync_enum reply = axi_out.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::AW_READY);
      aw_state = ACK;
    }
  }

  // W channel
  if (w_state == CLEAR && !w_queue_out.empty()) {
    ARM::AXI::Payload *payload = w_queue_out.front();
    ARM::AXI::Phase phase = (w_beat_count + 1 == payload->get_beat_count())
                                ? ARM::AXI::W_VALID_LAST
                                : ARM::AXI::W_VALID;

    w_state = REQ;
    tlm_sync_enum reply = axi_out.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::W_READY);
      w_state = ACK;
    }
  }

  // B channel
  if (b_state == CLEAR && !b_queue_out.empty()) {
    ARM::AXI::Payload *payload = b_queue_out.front();
    ARM::AXI::Phase phase = ARM::AXI::B_VALID;

    b_state = REQ;
    tlm_sync_enum reply = axi_in.nb_transport_bw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::B_READY);
      b_state = ACK;
    }
  }

  // AR channel
  if (ar_state == CLEAR && !ar_queue_out.empty()) {
    ARM::AXI::Payload *payload = ar_queue_out.front();
    ARM::AXI::Phase phase = ARM::AXI::AR_VALID;

    ar_state = REQ;
    tlm_sync_enum reply = axi_out.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::AR_READY);
      ar_state = ACK;
    }
  }

  // R channel
  if (r_state == CLEAR && !r_queue_out.empty()) {
    ARM::AXI::Payload *payload = r_queue_out.front();
    ARM::AXI::Phase phase = (r_beat_count + 1 == payload->get_beat_count())
                                ? ARM::AXI::R_VALID_LAST
                                : ARM::AXI::R_VALID;

    r_state = REQ;
    tlm_sync_enum reply = axi_in.nb_transport_bw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::R_READY);
      r_state = ACK;
    }
  }
}

Flit GenericInterconnect::read_flit_from_buffer(const uint8_t *src) {
  size_t offset = overhead_size;
  Flit flit(flit_data_bytes);

  std::memcpy(&flit.axi_ch, src + offset, sizeof(flit.axi_ch));
  offset += sizeof(flit.axi_ch);
  std::memcpy(&flit.len, src + offset, sizeof(flit.len));
  offset += sizeof(flit.len);
  std::memcpy(&flit.burst, src + offset, sizeof(flit.burst));
  offset += sizeof(flit.burst);
  std::memcpy(&flit.id, src + offset, sizeof(flit.id));
  offset += sizeof(flit.id);
  std::memcpy(&flit.user, src + offset, sizeof(flit.user));
  offset += sizeof(flit.user);
  std::memcpy(flit.axi_data.data.data(), src + offset, flit_data_bytes);

  return flit;
}

void GenericInterconnect::write_flit_to_buffer(uint8_t *dest, const Flit &flit,
                                               size_t flit_payload_bytes,
                                               size_t flit_padding_bytes) {
  size_t offset = 0;

  // Protocol overhead
  std::memset(dest + offset, 0xFF, overhead_size);
  offset += overhead_size;

  // Header
  std::memcpy(dest + offset, &flit.axi_ch, sizeof(flit.axi_ch));
  offset += sizeof(flit.axi_ch);
  std::memcpy(dest + offset, &flit.len, sizeof(flit.len));
  offset += sizeof(flit.len);
  std::memcpy(dest + offset, &flit.burst, sizeof(flit.burst));
  offset += sizeof(flit.burst);
  std::memcpy(dest + offset, &flit.id, sizeof(flit.id));
  offset += sizeof(flit.id);
  std::memcpy(dest + offset, &flit.user, sizeof(flit.user));
  offset += sizeof(flit.user);

  // Payload + padding
  std::memcpy(dest + offset, flit.axi_data.data.data(), flit_payload_bytes);
  offset += flit_payload_bytes;
  std::memset(dest + offset, 0, flit_padding_bytes);
}

void GenericInterconnect::forward_flit(unsigned rx_idx, uint8_t dest_id) {
  const unsigned tx_idx = Router::instance().get_link_id(chiplet_id, dest_id);
  if (tx_idx == -1)
    SC_LOG_ERROR(this, "No valid routing path from " << chiplet_id << " to "
                                                     << int(dest_id));

  const size_t tail = tx_ptrs[tx_idx];
  const size_t room = tx_buffers[tx_idx].size() - tail;

  if (room >= flit_size) {
    // Copy to Tx buffer
    std::memcpy(&tx_buffers[tx_idx][tail], rx_buffers[rx_idx].data(),
                flit_size);
    tx_ptrs[tx_idx] += flit_size;
    // Consume from Rx buffer
    std::memmove(rx_buffers[rx_idx].data(),
                 rx_buffers[rx_idx].data() + flit_size,
                 rx_ptrs[rx_idx] - flit_size);
    rx_ptrs[rx_idx] -= flit_size;
  }
}

void GenericInterconnect::process_flit(unsigned rx_idx, Flit &flit) {
  switch (flit.axi_ch) {
  case AW: {
    uint32_t address;
    std::memcpy(&address, flit.axi_data.data.data(), sizeof(uint32_t));
    ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
        ARM::AXI::COMMAND_WRITE, address, get_axi_size(axi_width), flit.len,
        flit.burst);
    payload->id = flit.id;
    payload->user = flit.user;
    // Save payload for later beats
    UserSignals user = UserSignals::decode(payload->user);
    PayloadKey key = {payload->id, user.core, user.source};
    manager_payloads[key] = payload;
    if (aw_state == CLEAR) {
      aw_queue_out.push_back(payload);
      // Consume from Rx buffer
      std::memmove(rx_buffers[rx_idx].data(),
                   rx_buffers[rx_idx].data() + flit_size,
                   rx_ptrs[rx_idx] - flit_size);
      rx_ptrs[rx_idx] -= flit_size;
    }
    break;
  }
  case W: {
    ARM::AXI::Payload *payload = nullptr;
    UserSignals user = UserSignals::decode(flit.user);
    auto it = manager_payloads.find({flit.id, user.core, user.source});
    if (it != manager_payloads.end())
      payload = it->second;
    else
      SC_LOG_ERROR(this, "AXI Protocol Violation: Unknown payload");
    if (w_state == CLEAR) {
      payload->write_in_beat(flit.axi_data.data.data() +
                             flit_w_beat_count * axi_width / 8);
      flit_w_beat_count++;
      w_queue_out.push_back(payload);
      // Remove if transaction is done or no more data in this flit
      if (w_beat_count + 1 == payload->get_beat_count() ||
          (flit_w_beat_count + 1) * axi_width / 8 > flit_data_bytes) {
        flit_w_beat_count = 0;
        // Consume from Rx buffer
        std::memmove(rx_buffers[rx_idx].data(),
                     rx_buffers[rx_idx].data() + flit_size,
                     rx_ptrs[rx_idx] - flit_size);
        rx_ptrs[rx_idx] -= flit_size;
      }
    }
    break;
  }
  case B: {
    ARM::AXI::Payload *payload = nullptr;
    UserSignals user = UserSignals::decode(flit.user);
    auto it = subordinate_payloads.find({flit.id, user.core, user.source});
    if (it != subordinate_payloads.end())
      payload = it->second;
    else
      SC_LOG_ERROR(this, "AXI Protocol Violation: Unknown payload");
    if (b_state == CLEAR) {
      b_queue_out.push_back(payload);
      // Consume from Rx buffer
      std::memmove(rx_buffers[rx_idx].data(),
                   rx_buffers[rx_idx].data() + flit_size,
                   rx_ptrs[rx_idx] - flit_size);
      rx_ptrs[rx_idx] -= flit_size;
    }
    break;
  }
  case AR: {
    uint32_t address;
    std::memcpy(&address, flit.axi_data.data.data(), sizeof(uint32_t));
    ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
        ARM::AXI::COMMAND_READ, address, get_axi_size(axi_width), flit.len,
        flit.burst);
    payload->id = flit.id;
    payload->user = flit.user;
    if (ar_state == CLEAR) {
      ar_queue_out.push_back(payload);
      // Consume from Rx buffer
      std::memmove(rx_buffers[rx_idx].data(),
                   rx_buffers[rx_idx].data() + flit_size,
                   rx_ptrs[rx_idx] - flit_size);
      rx_ptrs[rx_idx] -= flit_size;
    }
    break;
  }
  case R: {
    ARM::AXI::Payload *payload = nullptr;
    UserSignals user = UserSignals::decode(flit.user);
    auto it = subordinate_payloads.find({flit.id, user.core, user.source});
    if (it != subordinate_payloads.end())
      payload = it->second;
    else
      SC_LOG_ERROR(this, "AXI Protocol Violation: Unknown payload");
    if (r_state == CLEAR) {
      payload->read_in_beat(flit.axi_data.data.data() +
                            flit_r_beat_count * axi_width / 8);
      flit_r_beat_count++;
      r_queue_out.push_back(payload);
      // Remove if transaction is done or no more data in this flit
      if (r_beat_count + 1 == payload->get_beat_count() ||
          (flit_r_beat_count + 1) * axi_width / 8 > flit_data_bytes) {
        flit_r_beat_count = 0;
        // Consume from Rx buffer
        std::memmove(rx_buffers[rx_idx].data(),
                     rx_buffers[rx_idx].data() + flit_size,
                     rx_ptrs[rx_idx] - flit_size);
        rx_ptrs[rx_idx] -= flit_size;
      }
    }
    break;
  }
  default:
    break;
  }
}

void GenericInterconnect::send_irq(ARM::AXI::Payload &payload) {
  if (num_cores == 0)
    return;

  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;

  tlm_generic_payload *irq = new tlm_generic_payload;

  irq->set_command(TLM_READ_COMMAND);
  irq->set_address(payload.get_address());
  irq->set_data_length(payload.get_data_length());

  SC_LOG_DEBUG(this, "Sending IRQ to Core0");
  irq_sockets[0]->nb_transport_fw(*irq, phase, delay);
}

void GenericInterconnect::erase_payload(
    std::unordered_map<PayloadKey, ARM::AXI::Payload *, PayloadKeyHash>
        &payload_map,
    ARM::AXI::Payload *payload) {
  UserSignals user = UserSignals::decode(payload->user);
  PayloadKey key = {payload->id, user.core, user.source};

  auto it = payload_map.find(key);
  if (it != payload_map.end()) {
    payload_map.erase(it);
    return;
  }

  SC_LOG_WARN(this, "Tried to erase a non-existent payload");
}

// -------------------------------------------------------
// Debug Functions
// -------------------------------------------------------
void GenericInterconnect::dump_staging_buffer() {
  std::cout << sc_time_stamp() << " === Staging Buffer " << name() << " ===\n";
  std::cout << "(used " << staging_buffer_ptr << " / " << staging_buffer.size()
            << " bytes)\n  ";

  for (size_t j = 0; j < staging_buffer_ptr; ++j) {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(staging_buffer[j]) << " ";
    if ((j + 1) % flit_size == 0)
      std::cout << "\n  ";
  }
  std::cout << std::dec << "\n";
}

void GenericInterconnect::dump_tx_buffers() {
  std::cout << sc_time_stamp() << " === Tx Buffers " << name() << " ===\n";
  for (size_t i = 0; i < tx_buffers.size(); ++i) {
    std::cout << "Buffer[" << i << "] (used " << tx_ptrs[i] << " / "
              << tx_buffers[i].size() << " bytes):\n  ";

    for (size_t j = 0; j < tx_ptrs[i]; ++j) {
      std::cout << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(tx_buffers[i][j]) << " ";
      if ((j + 1) % flit_size == 0)
        std::cout << "\n  ";
    }
    std::cout << std::dec << "\n";
  }
}

void GenericInterconnect::dump_rx_buffers() {
  std::cout << sc_time_stamp() << " === Rx Buffers " << name() << " ===\n";
  for (size_t i = 0; i < rx_buffers.size(); ++i) {
    std::cout << "Buffer[" << i << "] (used " << rx_ptrs[i] << " / "
              << rx_buffers[i].size() << " bytes):\n  ";

    for (size_t j = 0; j < rx_ptrs[i]; ++j) {
      std::cout << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(rx_buffers[i][j]) << " ";
      if ((j + 1) % flit_size == 0)
        std::cout << "\n  ";
    }
    std::cout << std::dec << "\n";
  }
}