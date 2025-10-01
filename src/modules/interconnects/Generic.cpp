#include "modules/interconnects/Generic.h"

#include "common/Router.h"

GenericInterconnect::GenericInterconnect(sc_module_name name,
                                         unsigned chiplet_id,
                                         ChipletConfig chiplet_config,
                                         InterconnectConfig interconnect_config,
                                         DMAEngine *dma_engine)
    : InterconnectBase(chiplet_config.connections.size(),
                       chiplet_config.config["cores"]["num"].as<unsigned>()),
      sc_module(name), chiplet_id(chiplet_id),
      num_cores(chiplet_config.config["cores"]["num"].as<unsigned>()),
      num_links(chiplet_config.connections.size()),
      axi_width(chiplet_config.config["axi"]["width"].as<unsigned>()),
      flit_size(
          interconnect_config.defaults["interconnect_protocol"]["flit_size"]
              .as<unsigned>()),
      overhead_size(
          interconnect_config.defaults["interconnect_protocol"]["overhead_size"]
              .as<unsigned>()),
      staging_buffer_size(
          interconnect_config.defaults["interconnect"]["staging_buffer_size"]
              .as<unsigned>()),
      link_buffer_size(
          interconnect_config.defaults["interconnect"]["link_buffer_size"]
              .as<unsigned>()),
      connections(chiplet_config.connections), dma_engine(dma_engine),
      axi_tsocket("axi_tsocket", *this,
                  &GenericInterconnect::nb_transport_fw_axi,
                  ARM::TLM::PROTOCOL_AXI4, axi_width),
      axi_isocket("axi_isocket", *this,
                  &GenericInterconnect::nb_transport_bw_axi,
                  ARM::TLM::PROTOCOL_AXI4, axi_width) {
  dma_vm_id = dma_engine->register_virtual_initiator(this);

  phy_tsockets =
      new simple_target_socket_tagged<GenericInterconnect>[num_links];
  phy_isockets =
      new simple_initiator_socket_tagged<GenericInterconnect>[num_links];

  for (int i = 0; i < num_links; ++i) {
    phy_tsockets[i].register_nb_transport_fw(
        this, &GenericInterconnect::nb_transport_fw_phy, i);
    phy_isockets[i].register_nb_transport_bw(
        this, &GenericInterconnect::nb_transport_bw_phy, i);
  }

  irq_sockets =
      new simple_initiator_socket_tagged<GenericInterconnect>[num_cores];

  // Register ports in InterconnectBase
  axi_in_port =
      reinterpret_cast<ARM::AXI::SimpleTargetSocket<InterconnectBase> *>(
          &axi_tsocket);
  axi_out_port =
      reinterpret_cast<ARM::AXI::SimpleInitiatorSocket<InterconnectBase> *>(
          &axi_isocket);
  for (int i = 0; i < num_links; ++i) {
    link_in_ports[i] =
        reinterpret_cast<simple_target_socket_tagged<InterconnectBase> *>(
            &phy_tsockets[i]);
    link_out_ports[i] =
        reinterpret_cast<simple_initiator_socket_tagged<InterconnectBase> *>(
            &phy_isockets[i]);
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

  flit_header_bytes = overhead_size + sizeof(bool) + 5 * sizeof(uint8_t) +
                      3 * sizeof(uint16_t) + sizeof(uint32_t);
  flit_data_bytes = flit_size - flit_header_bytes;

  SC_METHOD(axi_clock_posedge);
  sensitive << axi_clock.pos();
  dont_initialize();

  SC_METHOD(protocol_clock_posedge);
  sensitive << protocol_clock.pos();
  dont_initialize();

  SC_METHOD(phy_clock_posedge);
  sensitive << phy_clock.pos();
  dont_initialize();
}

GenericInterconnect::~GenericInterconnect() {
  delete[] phy_tsockets;
  delete[] phy_isockets;
  delete[] irq_sockets;
}

void GenericInterconnect::bind_clock(sc_clock &clk) {
  axi_clock.bind(clk);
  protocol_clock.bind(clk);
  phy_clock.bind(clk);
}

// ============================================================================
// AXI Clock - Handle AXI side transactions and staging buffer input
// ============================================================================
void GenericInterconnect::axi_clock_posedge() {
  if (b_state == ACK)
    b_state = CLEAR;
  if (r_state == ACK)
    r_state = CLEAR;

  // -------------------------------------------------------
  // Handle READ REQUEST (AR channel)
  // -------------------------------------------------------
  if (!r_outgoing && !ar_queue.empty()) {
    auto *payload = ar_queue.front();
    ar_queue.pop_front();
    r_outgoing = payload;

    const unsigned beats = payload->get_beat_count();
    const unsigned beat_bytes = payload->get_beat_data_length();
    const size_t total_len = beats * beat_bytes;
    flit_count = 1; // always 1 flit for read request

    // Decode AXI signals
    const UserSignals user = UserSignals::decode(payload->user);
    request_id = payload->id;
    core_id = user.core;
    source_id = user.source;
    destination_id = user.destination;
    address = payload->get_address();
    size = payload->get_data_length();
    fixed_address = user.fixed_address;

    // Dummy read to satisfy AXI payload semantics
    std::vector<uint8_t> dummy(total_len);
    payload->read_in(dummy.data());
  }

  // -------------------------------------------------------
  // Handle WRITE REQUEST (AW channel)
  // -------------------------------------------------------
  if (!aw_queue.empty()) {
    auto *payload = aw_queue.front();
    aw_queue.pop_front();

    const unsigned beats = payload->get_beat_count();
    const unsigned beat_bytes = payload->get_beat_data_length();
    const size_t total_len = beats * beat_bytes;
    flit_count = (total_len + flit_data_bytes - 1) / flit_data_bytes;

    // Decode AXI signals
    const UserSignals user = UserSignals::decode(payload->user);
    request_id = payload->id;
    core_id = user.core;
    source_id = user.source;
    destination_id = user.destination;
    address = payload->get_address();
    fixed_address = user.fixed_address;
  }

  // -------------------------------------------------------
  // Handle WRITE DATA beats (W channel)
  // -------------------------------------------------------
  if (!w_queue.empty()) {
    auto *payload = w_queue.front();
    const size_t beat_bytes = payload->get_beat_data_length();
    const size_t free_bytes = staging_buffer.size() - staging_buffer_ptr;

    if (free_bytes >= beat_bytes) {
      // Copy data into staging buffer
      payload->write_out_beat(beat_idx, &staging_buffer[staging_buffer_ptr]);
      beat_idx += 1;
      staging_buffer_ptr += beat_bytes;

      // Consume beat
      w_queue.pop_front();

      // Prepare write response if this was the last beat
      if (axi_wlast_beat && w_queue.empty()) {
        b_outgoing = payload;
      } else {
        payload->unref(); // done with this beat
      }
    }
  }
}

// ============================================================================
// Protocol Clock - Construct and process flits in link buffers
// ============================================================================
void GenericInterconnect::protocol_clock_posedge() {
  // -------------------------------------------------------
  // Build READ REQUEST flit
  // -------------------------------------------------------
  if (r_outgoing && !protocol_rreq_flit_sent) {
    std::vector<uint8_t> flit(flit_size, 0);

    FlitHeader header{flit_count,   0,         request_id,
                      core_id,      source_id, destination_id,
                      READ_COMMAND, address,   size,
                      fixed_address};

    write_flit_header(flit.data(), header);

    const unsigned tx_idx =
        Router::instance().get_link_id(chiplet_id, destination_id);
    const size_t tail = tx_ptrs[tx_idx];
    const size_t room = tx_buffers[tx_idx].size() - tail;

    if (room >= flit_size) {
      std::memcpy(&tx_buffers[tx_idx][tail], flit.data(), flit_size);
      tx_ptrs[tx_idx] += flit_size;
      protocol_rreq_flit_sent = true;
    }
  }

  // -------------------------------------------------------
  // Send READ RESPONSE (R channel)
  // -------------------------------------------------------
  if (r_outgoing && protocol_rreq_flit_sent) {
    ARM::AXI::Phase phase = ARM::AXI::R_VALID_LAST;

    r_state = REQ;
    const tlm_sync_enum reply = axi_tsocket.nb_transport_bw(*r_outgoing, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::R_READY);
      r_state = ACK;
      r_outgoing->unref();
      r_outgoing = nullptr;
      axi_active_tx = false;
    }
  }

  // -------------------------------------------------------
  // Send WRITE RESPONSE (B channel)
  // -------------------------------------------------------
  if (b_outgoing) {
    ARM::AXI::Phase phase = ARM::AXI::B_VALID;

    b_state = REQ;
    const tlm_sync_enum reply = axi_tsocket.nb_transport_bw(*b_outgoing, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::B_READY);
      b_state = ACK;
      b_outgoing->unref();
      b_outgoing = nullptr;
    }
  }

  // -------------------------------------------------------
  // Process staging buffer -> build WRITE flits
  // -------------------------------------------------------
  if (staging_buffer_ptr > 0) {
    uint16_t flit_payload = 0;

    if (staging_buffer_ptr >= flit_data_bytes) {
      flit_payload = flit_data_bytes;
    } else if (axi_wlast_beat && w_queue.empty()) {
      flit_payload = staging_buffer_ptr; // final, possibly padded flit
    } else if (axi_rlast_beat) {
      flit_payload = staging_buffer_ptr; // final, possibly padded flit
    }

    if (flit_payload > 0) {
      std::vector<uint8_t> flit(flit_size, 0);

      FlitHeader header{flit_count,   flit_id,        request_id,    core_id,
                        source_id,    destination_id, WRITE_COMMAND, address,
                        flit_payload, fixed_address};

      write_flit_header(flit.data(), header);

      std::memcpy(flit.data() + flit_header_bytes, staging_buffer.data(),
                  flit_payload);

      const unsigned tx_idx =
          Router::instance().get_link_id(chiplet_id, destination_id);
      const size_t tail = tx_ptrs[tx_idx];
      const size_t room = tx_buffers[tx_idx].size() - tail;

      if (room >= flit_size) {
        std::memcpy(&tx_buffers[tx_idx][tail], flit.data(), flit_size);
        tx_ptrs[tx_idx] += flit_size;

        // Consume from staging buffer
        std::memmove(staging_buffer.data(),
                     staging_buffer.data() + flit_payload,
                     staging_buffer_ptr - flit_payload);
        staging_buffer_ptr -= flit_payload;

        // Increment for next flit
        address += flit_payload;
        flit_id++;
      }
    }

    // Reset after last beat
    if (axi_wlast_beat && w_queue.empty() && staging_buffer_ptr == 0) {
      axi_active_tx = false;
      axi_wlast_beat = false;
      beat_idx = 0;
      flit_count = 0;
      flit_id = 0;
    } else if (axi_rlast_beat && staging_buffer_ptr == 0) {
      axi_active_tx = false;
      axi_active_read = false;
      axi_rlast_beat = false;
      beat_idx = 0;
      flit_count = 0;
      flit_id = 0;
    }
  }

  // -------------------------------------------------------
  // Process Rx buffers
  // -------------------------------------------------------
  for (size_t rx_idx = 0; rx_idx < rx_buffers.size(); ++rx_idx) {
    if (rx_ptrs[rx_idx] < flit_size || axi_active_rx)
      continue;

    auto *flit_base = rx_buffers[rx_idx].data();
    FlitHeader flit_header;

    size_t offset = read_flit_header(flit_base, flit_header);

    bool at_source = flit_header.source_id == chiplet_id;
    bool at_destination = flit_header.destination_id == chiplet_id;
    bool is_read = flit_header.command == READ_COMMAND;
    bool is_write = flit_header.command == WRITE_COMMAND;

    // at source and read operation:
    //    transaction was an off-chip read request
    //    -> set to write operation, send to RAM via AXI
    //    -> send IRQ to core
    // at destination and read operation:
    //    transaction is an off-chip read request
    //    -> send to RAM via AXI
    //    -> send back to source via interconnects (backward path!)
    // at destination and write operation:
    //    transaction is an off-chip write request
    //    -> send to RAM via AXI
    //    -> send IRQ to core
    // not at source or destination
    //    transaction is not at the destination
    //    -> send to destination via interconnects

    if (at_source) {
      unsigned axi_bytes = axi_width / 8;
      unsigned beats = (flit_header.size + axi_bytes - 1) / axi_bytes;
      uint8_t len = beats - 1;
      ARM::AXI::Size size = get_axi_size(axi_width);

      ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
          ARM::AXI::COMMAND_WRITE, flit_header.address, size, len,
          ARM::AXI::BURST_INCR);

      UserSignals user;
      user.core = flit_header.core_id;
      user.source = flit_header.source_id;
      user.destination = flit_header.destination_id;
      user.fixed_address = flit_header.fixed_address;
      user.flit_count = flit_header.flit_count;

      payload->id = request_id;
      payload->user = user.encode();
      payload->cache = ARM::AXI::CACHE_AR_DEVICE_NB;

      payload->write_in(flit_base + offset);

      bool result = send_dma_request(*payload);

      if (result) {
        axi_active_rx = true;
        axi_active_rx_idx = rx_idx;
      }
    } else if (at_destination) {
      unsigned axi_bytes = axi_width / 8;
      unsigned beats = (flit_header.size + axi_bytes - 1) / axi_bytes;
      uint8_t len = beats - 1;
      ARM::AXI::Size size = get_axi_size(axi_width);
      ARM::AXI::Command command =
          is_read ? ARM::AXI::COMMAND_READ : ARM::AXI::COMMAND_WRITE;

      ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
          command, flit_header.address, size, len, ARM::AXI::BURST_INCR);

      UserSignals user;
      user.core = flit_header.core_id;
      user.source = flit_header.source_id;
      user.destination = flit_header.destination_id;
      user.fixed_address = flit_header.fixed_address;
      user.flit_count = flit_header.flit_count;

      payload->id = request_id;
      payload->user = user.encode();
      payload->cache = ARM::AXI::CACHE_AR_DEVICE_NB;

      if (is_write)
        payload->write_in(flit_base + offset);

      bool result = send_dma_request(*payload);

      if (result) {
        axi_active_rx = true;
        axi_active_rx_idx = rx_idx;
        axi_active_flit_id = flit_header.flit_id;
      }
    } else {
      const unsigned tx_idx = Router::instance().get_link_id(
          chiplet_id, flit_header.destination_id);
      const size_t tail = tx_ptrs[tx_idx];
      const size_t room = tx_buffers[tx_idx].size() - tail;

      if (room >= flit_size) {
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
  }
}

// ============================================================================
// PHY Clock - Send flits to PHY sockets and receive into Rx buffers
// ============================================================================
void GenericInterconnect::phy_clock_posedge() {
  // -------------------------------------------------------
  // Send from Tx buffers
  // -------------------------------------------------------
  for (size_t tx_idx = 0; tx_idx < tx_buffers.size(); ++tx_idx) {
    if (tx_ptrs[tx_idx] < flit_size || phy_active_tx[tx_idx])
      continue;

    auto *flit_base = tx_buffers[tx_idx].data();

    // Construct flit transaction
    auto *flit = new tlm::tlm_generic_payload;
    flit->set_command(TLM_WRITE_COMMAND); // encoded in flit
    flit->set_address(0);                 // encoded in flit
    flit->set_data_ptr(flit_base);
    flit->set_data_length(flit_size);

    tlm_phase phase = BEGIN_REQ;
    sc_time delay = SC_ZERO_TIME;
    const tlm_sync_enum reply =
        phy_isockets[tx_idx]->nb_transport_fw(*flit, phase, delay);

    if (reply == TLM_UPDATED) {
      phy_active_tx[tx_idx] = true;
    } else {
      delete flit;
    }
  }

  // -------------------------------------------------------
  // Receive into Rx buffers
  // -------------------------------------------------------
  while (!phy_queue.empty()) {
    PHYRequest request = phy_queue.front();
    phy_queue.pop_front();

    const int rx_idx = request.interconnect_id;
    const size_t tail = rx_ptrs[rx_idx];
    const size_t room = rx_buffers[rx_idx].size() - tail;

    if (room >= flit_size) {
      std::memcpy(&rx_buffers[rx_idx][tail],
                  request.transaction->get_data_ptr(), flit_size);
      rx_ptrs[rx_idx] += flit_size;

      tlm_phase phase = BEGIN_RESP;
      sc_time delay = SC_ZERO_TIME;
      phy_tsockets[rx_idx]->nb_transport_bw(*request.transaction, phase, delay);
    }
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum
GenericInterconnect::nb_transport_fw_axi(ARM::AXI::Payload &payload,
                                         ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AR_VALID:
    if (axi_active_tx)
      return TLM_ACCEPTED; // backpressure
    axi_active_tx = true;
    ar_queue.push_back(&payload);
    payload.ref();
    phase = ARM::AXI::AR_READY;
    return TLM_UPDATED;
  case ARM::AXI::R_READY:
    r_state = ACK;
    return TLM_ACCEPTED;
  case ARM::AXI::AW_VALID:
    if (axi_active_tx)
      return TLM_ACCEPTED; // backpressure
    axi_active_tx = true;
    aw_queue.push_back(&payload);
    payload.ref();
    phase = ARM::AXI::AW_READY;
    return TLM_UPDATED;
  case ARM::AXI::W_VALID:
    if (staging_buffer_ptr + axi_width > staging_buffer_size)
      return TLM_ACCEPTED; // backpressure
    w_queue.push_back(&payload);
    payload.ref();
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  case ARM::AXI::W_VALID_LAST:
    if (staging_buffer_ptr + axi_width > staging_buffer_size)
      return TLM_ACCEPTED; // backpressure
    axi_wlast_beat = true;
    w_queue.push_back(&payload);
    payload.ref();
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  case ARM::AXI::B_READY:
    b_state = ACK;
    return TLM_ACCEPTED;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unrecognized phase");
    return TLM_ACCEPTED;
  }
}

tlm_sync_enum
GenericInterconnect::nb_transport_bw_axi(ARM::AXI::Payload &payload,
                                         ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AR_READY:
    return TLM_ACCEPTED;
  case ARM::AXI::R_VALID:
    if (axi_active_tx && !axi_active_read)
      return TLM_ACCEPTED; // backpressure
    axi_active_tx = true;
    axi_active_read = true;
    {
      const unsigned beats = payload.get_beat_count();
      const unsigned beat_bytes = payload.get_beat_data_length();
      const size_t total_len = beats * beat_bytes;
      flit_count = (total_len + flit_data_bytes - 1) / flit_data_bytes;

      // Decode AXI signals
      const UserSignals user = UserSignals::decode(payload.user);
      request_id = payload.id;
      core_id = user.core;
      source_id = user.source;
      destination_id = user.source;
      address = payload.get_address();
      fixed_address = user.fixed_address;
    }
    {
      const size_t beat_bytes = payload.get_beat_data_length();
      const int rx_idx = axi_active_rx_idx;
      // Copy from payload buffer into staging buffer
      payload.read_out_beat(beat_idx, &staging_buffer[staging_buffer_ptr]);
      beat_idx += 1;
      staging_buffer_ptr += beat_bytes;
      dump_staging_buffer();
    }
    phase = ARM::AXI::R_READY;
    return TLM_UPDATED;
  case ARM::AXI::R_VALID_LAST: {
    const size_t beat_bytes = payload.get_beat_data_length();
    const int rx_idx = axi_active_rx_idx;
    // Copy from payload buffer into staging buffer
    payload.read_out_beat(beat_idx, &staging_buffer[staging_buffer_ptr]);
    staging_buffer_ptr += beat_bytes;
    // Consume read request flit from Rx buffer
    std::memmove(rx_buffers[rx_idx].data(),
                 rx_buffers[rx_idx].data() + flit_size,
                 rx_ptrs[rx_idx] - flit_size);
    rx_ptrs[rx_idx] -= flit_size;
    dump_staging_buffer();
  }
    axi_rlast_beat = true;
    phase = ARM::AXI::R_READY;
    return TLM_UPDATED;
  case ARM::AXI::AW_READY:
    return TLM_ACCEPTED;
  case ARM::AXI::W_READY:
    return TLM_ACCEPTED;
  case ARM::AXI::B_VALID: {
    const int rx_idx = axi_active_rx_idx;
    // Consume from Rx buffer
    std::memmove(rx_buffers[rx_idx].data(),
                 rx_buffers[rx_idx].data() + flit_size,
                 rx_ptrs[rx_idx] - flit_size);
    rx_ptrs[rx_idx] -= flit_size;
  }
    {
      // Decode AXI signals
      const UserSignals user = UserSignals::decode(payload.user);
      uint16_t active_flit_count = user.flit_count;
      // IRQ to cores
      if (axi_active_flit_id == active_flit_count - 1) {
        send_irq(payload);
      }
    }
    axi_active_rx = false;
    axi_active_rx_idx = -1;
    phase = ARM::AXI::B_READY;
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
  SC_LOG_DEBUG(this,
               "Interconnect TLM Protocol " << "PHY" << id << ": " << phase);

  switch (phase) {
  case BEGIN_REQ:
    if (rx_ptrs[id] + flit_size > rx_buffers[id].size())
      return TLM_ACCEPTED; // backpressure

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
  SC_LOG_DEBUG(this,
               "Interconnect TLM Protocol " << "PHY" << id << ": " << phase);

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
// helper functions
// -------------------------------------------------------
size_t GenericInterconnect::read_flit_header(
    uint8_t *flit_base, GenericInterconnect::FlitHeader &flit_header) {
  // interconnect transport overhead
  size_t offset = overhead_size;

  std::memcpy(&flit_header.flit_count, flit_base + offset, sizeof(uint16_t));
  offset += sizeof(uint16_t);
  std::memcpy(&flit_header.flit_id, flit_base + offset, sizeof(uint16_t));
  offset += sizeof(uint16_t);
  std::memcpy(&flit_header.request_id, flit_base + offset, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(&flit_header.core_id, flit_base + offset, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(&flit_header.source_id, flit_base + offset, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(&flit_header.destination_id, flit_base + offset, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(&flit_header.command, flit_base + offset, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(&flit_header.address, flit_base + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  std::memcpy(&flit_header.size, flit_base + offset, sizeof(uint16_t));
  offset += sizeof(uint16_t);
  std::memcpy(&flit_header.fixed_address, flit_base + offset, sizeof(bool));
  offset += sizeof(bool);

  return offset;
}

size_t GenericInterconnect::write_flit_header(
    uint8_t *flit_base, GenericInterconnect::FlitHeader &flit_header) {
  // interconnect transport overhead
  std::memset(flit_base, 0, overhead_size);
  size_t offset = overhead_size;

  std::memcpy(flit_base + offset, &flit_header.flit_count, sizeof(uint16_t));
  offset += sizeof(uint16_t);
  std::memcpy(flit_base + offset, &flit_header.flit_id, sizeof(uint16_t));
  offset += sizeof(uint16_t);
  std::memcpy(flit_base + offset, &flit_header.request_id, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(flit_base + offset, &flit_header.core_id, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(flit_base + offset, &flit_header.source_id, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(flit_base + offset, &flit_header.destination_id, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(flit_base + offset, &flit_header.command, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(flit_base + offset, &flit_header.address, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  std::memcpy(flit_base + offset, &flit_header.size, sizeof(uint16_t));
  offset += sizeof(uint16_t);
  std::memcpy(flit_base + offset, &flit_header.fixed_address, sizeof(bool));
  offset += sizeof(bool);

  return offset;
}

void GenericInterconnect::send_irq(ARM::AXI::Payload &payload) {
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  tlm_sync_enum tlm_resp;

  const UserSignals user = UserSignals::decode(payload.user);

  unsigned int data_size =
      (user.flit_count - 1) * flit_data_bytes + payload.get_data_length();

  unsigned int address_offset = (user.flit_count - 1) * flit_data_bytes;

  tlm_generic_payload *irq = new tlm_generic_payload;

  irq->set_command(TLM_READ_COMMAND);
  irq->set_address(payload.get_address() - address_offset);
  irq->set_data_length(data_size);

  if (user.destination == user.source) {
    // send read IRQs to request core
    SC_LOG_DEBUG(this, "Sending IRQ to Core" << user.core);
    tlm_resp = irq_sockets[user.core]->nb_transport_fw(*irq, phase, delay);
  } else {
    // send write IRQs to Core0
    SC_LOG_DEBUG(this, "Sending IRQ to Core0");
    tlm_resp = irq_sockets[0]->nb_transport_fw(*irq, phase, delay);
  }
}

// -------------------------------------------------------
// debug functions
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