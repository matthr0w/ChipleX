#include "modules/Interconnect.h"

#include "common/RoutingTable.h"

Interconnect::Interconnect(sc_module_name name, unsigned int chip_id,
                           unsigned int num_cores,
                           unsigned int num_interconnects,
                           unsigned int buffer_size, unsigned int flit_size,
                           unsigned int overhead_size, double bandwidth,
                           double distance, unsigned int axi_width)
    : sc_module(name), chip_id(chip_id), buffer_size(buffer_size),
      flit_size(flit_size), overhead_size(overhead_size), bandwidth(bandwidth),
      distance(distance), axi_width(axi_width),
      utilization_tracker(this->name()),
      axi_tsocket("axi_tsocket", *this, &Interconnect::nb_transport_fw_axi,
                  ARM::TLM::PROTOCOL_AXI4, axi_width),
      clock("clock") {
  phy_tsockets =
      new simple_target_socket_tagged<Interconnect>[num_interconnects];
  phy_isockets =
      new simple_initiator_socket_tagged<Interconnect>[num_interconnects];

  for (unsigned int i = 0; i < num_interconnects; ++i) {
    phy_tsockets[i].register_nb_transport_fw(
        this, &Interconnect::nb_transport_fw_phy, i);
    phy_isockets[i].register_nb_transport_bw(
        this, &Interconnect::nb_transport_bw_phy, i);
  }

  irq_sockets = new simple_initiator_socket_tagged<Interconnect>[num_cores];

  staging_buffer.resize(1024, 0);

  tx_buffers.resize(num_interconnects, std::vector<uint8_t>(buffer_size, 0));
  rx_buffers.resize(num_interconnects, std::vector<uint8_t>(buffer_size, 0));

  tx_ptrs.resize(tx_buffers.size(), 0);
  rx_ptrs.resize(rx_buffers.size(), 0);

  flit_header_bytes = overhead_size + sizeof(bool) + 5 * sizeof(uint8_t) +
                      3 * sizeof(uint16_t) + sizeof(uint32_t);
  flit_data_bytes = flit_size - flit_header_bytes;

  SC_METHOD(clock_posedge);
  sensitive << clock.pos();
  dont_initialize();

  SC_METHOD(clock_negedge);
  sensitive << clock.neg();
  dont_initialize();
}

Interconnect::~Interconnect() {
  delete[] phy_tsockets;
  delete[] phy_isockets;
}

// at source and read operation:
//    transaction was an off-chip read request
//    -> set to write operation, send to RAM via AXI
//    -> send IRQ to core
// at destination and read operation:
//    transaction is an off-chip read request
//    -> send to RAM via AXI
//    -> send back to source via interconnects
// at destination and write operation:
//    transaction is an off-chip write request
//    -> send to RAM via AXI
//    -> send IRQ to core
// not at source or destination
//    transaction is not at the destination
//    -> send to destination via interconnects
// if (at_source && read_op) {
//   transaction->set_command(TLM_WRITE_COMMAND);
//   send_axi_request(*transaction);
//   if (flit_id == flit_count - 1) { // send IRQ on last flit
//     send_irq(*transaction, TLM_READ_COMMAND);
//   }
// } else if (at_destination) {
//   if (read_op) {
//     send_axi_request(*transaction);
//     send_phy_request(*transaction);
//   } else if (write_op) {
//     send_axi_request(*transaction);
//     if (flit_id == flit_count - 1) { // send IRQ on last flit
//       send_irq(*transaction, TLM_WRITE_COMMAND);
//     }
//   }
// } else {
//   send_phy_request(*transaction);
// }

void Interconnect::clock_posedge() {
  // -------------------------------------------------------
  // AXI side
  // -------------------------------------------------------
  if (b_state == ACK)
    b_state = CLEAR;
  if (r_state == ACK)
    r_state = CLEAR;

  // --- READ REQUEST ---
  if (!r_outgoing && !ar_queue.empty()) {
    ARM::AXI::Payload *p = ar_queue.front();
    r_outgoing = ar_queue.front();
    ar_queue.pop_front();

    unsigned beats = p->get_beat_count();
    unsigned beat_bytes = p->get_beat_data_length();
    const unsigned total_len = beats * beat_bytes;
    flit_count = 1;

    UserSignals user = UserSignals::decode(p->user);
    request_id = p->id;
    core_id = user.core;
    source_id = user.source;
    destination_id = user.destination;
    address = p->get_address();
    size = p->get_data_length();
    fixed_address = user.fixed_address;

    // read in garbage data to prevent library assertion
    std::vector<uint8_t> staging(total_len);
    p->read_in(staging.data());
  }

  // --- WRITE REQUEST ---
  if (!aw_queue.empty()) {
    ARM::AXI::Payload *p = aw_queue.front();
    aw_queue.pop_front();

    unsigned beats = p->get_beat_count();
    unsigned beat_bytes = p->get_beat_data_length();
    const unsigned total_len = beats * beat_bytes;
    flit_count = (total_len + flit_data_bytes - 1) / flit_data_bytes;

    UserSignals user = UserSignals::decode(p->user);
    request_id = p->id;
    core_id = user.core;
    source_id = user.source;
    destination_id = user.destination;
    address = p->get_address();
    fixed_address = user.fixed_address;
  }

  if (!w_queue.empty()) {
    ARM::AXI::Payload *p = w_queue.front();

    const size_t beat_bytes = p->get_beat_data_length();
    const size_t free_bytes = staging_buffer.size() - staging_buffer_ptr;

    if (free_bytes >= beat_bytes) {
      p->write_out_beat(beat_idx, &staging_buffer[staging_buffer_ptr]);

      beat_idx += 1;
      staging_buffer_ptr += beat_bytes;

      // remove beat
      w_queue.pop_front();

      dump_staging_buffer();

      // if this beat was the last: prepare write response
      if (wlast && w_queue.empty()) {
        b_outgoing = p;
      } else {
        p->unref(); // done with this beat
      }
    }
  }
}

void Interconnect::clock_negedge() {
  // --- READ request ---
  if (r_outgoing && !r_flit_sent) {
    // build request flit (header + all padding in data region)
    std::vector<uint8_t> flit(flit_size, 0);

    write_flit_header(flit.data(), flit_count, 0, request_id, core_id,
                      source_id, destination_id, READ_COMMAND, address, size,
                      fixed_address);

    // push into tx buffer
    const unsigned tx_idx = RoutingTable::get_route(chip_id, destination_id);
    const size_t tail = tx_ptrs[tx_idx];
    const size_t room = tx_buffers[tx_idx].size() - tail;

    if (room >= flit_size) {
      std::memcpy(&tx_buffers[tx_idx][tail], flit.data(), flit_size);
      tx_ptrs[tx_idx] += flit_size;
      r_flit_sent = true;
      dump_phy_buffers();
    }
  }

  if (r_outgoing && r_flit_sent) {
    ARM::AXI::Phase phase = ARM::AXI::R_VALID_LAST;

    r_state = REQ;
    tlm_sync_enum reply = axi_tsocket.nb_transport_bw(*r_outgoing, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::R_READY);
      r_state = ACK;
      r_outgoing->unref();
      r_outgoing = nullptr;
      active_txn = false;
    }
  }

  // --- WRITE RESPONSE ---
  if (b_outgoing) {
    ARM::AXI::Phase phase = ARM::AXI::B_VALID;

    b_state = REQ;
    tlm_sync_enum reply = axi_tsocket.nb_transport_bw(*b_outgoing, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::B_READY);
      b_state = ACK;
      b_outgoing->unref();
      b_outgoing = nullptr;
    }
  }

  // --- STAGING BUFFER ---
  if (staging_buffer_ptr > 0) {
    uint16_t flit_payload = 0;

    if (staging_buffer_ptr >= flit_data_bytes) {
      flit_payload = flit_data_bytes;
    } else if (wlast && w_queue.empty()) {
      flit_payload = staging_buffer_ptr;
    }

    if (flit_payload > 0) {
      // build flit
      std::vector<uint8_t> flit(flit_size, 0);

      write_flit_header(flit.data(), flit_count, flit_id, request_id, core_id,
                        source_id, destination_id, WRITE_COMMAND, address,
                        flit_payload, fixed_address);

      std::memcpy(flit.data() + flit_header_bytes, staging_buffer.data(),
                  flit_payload);

      // push into tx buffer
      const unsigned tx_idx = RoutingTable::get_route(chip_id, destination_id);
      const size_t tail = tx_ptrs[tx_idx];
      const size_t room = tx_buffers[tx_idx].size() - tail;

      if (room >= flit_size) {
        std::memcpy(&tx_buffers[tx_idx][tail], flit.data(), flit_size);
        tx_ptrs[tx_idx] += flit_size;
        staging_buffer_ptr -= flit_payload;
        flit_id++;
      }
    }

    // if wlast and staging buffer empty: reset
    if (wlast && staging_buffer_ptr == 0) {
      active_txn = false;
      wlast = false;
      beat_idx = 0;
      flit_count = 0;
      flit_id = 0;
      dump_staging_buffer();
      dump_phy_buffers();
    }
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum Interconnect::nb_transport_fw_axi(ARM::AXI::Payload &payload,
                                                ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AR_VALID:
    if (active_txn)
      return TLM_ACCEPTED; // backpressure
    active_txn = true;
    ar_queue.push_back(&payload);
    payload.ref();
    phase = ARM::AXI::AR_READY;
    return TLM_UPDATED;
  case ARM::AXI::R_READY:
    r_state = ACK;
    return TLM_ACCEPTED;
  case ARM::AXI::AW_VALID:
    if (active_txn)
      return TLM_ACCEPTED; // backpressure
    active_txn = true;
    aw_queue.push_back(&payload);
    payload.ref();
    phase = ARM::AXI::AW_READY;
    return TLM_UPDATED;
  case ARM::AXI::W_VALID:
    if (staging_buffer_ptr + axi_width > 1024)
      return TLM_ACCEPTED; // backpressure
    w_queue.push_back(&payload);
    payload.ref();
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  case ARM::AXI::W_VALID_LAST:
    if (staging_buffer_ptr + axi_width > 1024)
      return TLM_ACCEPTED; // backpressure
    wlast = true;
    w_queue.push_back(&payload);
    payload.ref();
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  case ARM::AXI::B_READY:
    b_state = ACK;
    return TLM_ACCEPTED;
  default:
    SC_REPORT_ERROR(name(), "AXI TLM Protocol: Unrecognized phase");
    return TLM_ACCEPTED;
  }
}

tlm_sync_enum
Interconnect::nb_transport_fw_phy(int id, tlm_generic_payload &transaction,
                                  tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction,
               "Interconnect TLM Protocol: " << phase << " - PHY" << id);

  switch (phase) {
  case BEGIN_REQ:

    delay += delays.transfer_delay(transaction);

    phy_request_queue.push_back(&transaction);

    phase = END_REQ;
    return TLM_UPDATED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum
Interconnect::nb_transport_bw_phy(int id, tlm_generic_payload &transaction,
                                  tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction,
               "Interconnect TLM Protocol: " << phase << " - PHY" << id);

  switch (phase) {
  case BEGIN_RESP:
    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

// -------------------------------------------------------
// helper functions
// -------------------------------------------------------
void Interconnect::write_flit_header(uint8_t *flit_base, uint16_t flit_count,
                                     uint16_t flit_id, uint8_t request_id,
                                     uint8_t core_id, uint8_t source_id,
                                     uint8_t destination_id, Command command,
                                     uint32_t address, uint16_t size,
                                     bool fixed_address) {
  // interconnect transport overhead
  std::memset(flit_base, 0, overhead_size);
  size_t offset = overhead_size;

  std::memcpy(flit_base + offset, &flit_count, sizeof(uint16_t));
  offset += sizeof(uint16_t);
  std::memcpy(flit_base + offset, &flit_id, sizeof(uint16_t));
  offset += sizeof(uint16_t);

  std::memcpy(flit_base + offset, &request_id, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(flit_base + offset, &core_id, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(flit_base + offset, &source_id, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(flit_base + offset, &destination_id, sizeof(uint8_t));
  offset += sizeof(uint8_t);

  std::memcpy(flit_base + offset, &command, sizeof(uint8_t));
  offset += sizeof(uint8_t);
  std::memcpy(flit_base + offset, &address, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  std::memcpy(flit_base + offset, &size, sizeof(uint16_t));
  offset += sizeof(uint16_t);

  std::memcpy(flit_base + offset, &fixed_address, sizeof(bool));
  offset += sizeof(bool);
}

// void Interconnect::send_phy_request(tlm_generic_payload &transaction) {
//   ChipletExtension *ext = nullptr;
//   tlm_phase phase = BEGIN_REQ;
//   sc_time delay = SC_ZERO_TIME;
//   tlm_sync_enum tlm_resp;

//   transaction.get_extension(ext);

//   int route = RoutingTable::get_route(chip_id, ext->destination_id);

//   if (route == -1) {
//     SC_LOG_ERROR(this, transaction,
//                  "ROUTING: Destination " << ext->destination_id
//                                          << " not available.");
//     return;
//   }

//   SC_LOG_DEBUG(this, transaction,
//                "ROUTING: Chip ID " << chip_id << " Destination "
//                                    << ext->destination_id
//                                    << " Route to Interconnect" << route);

//   tlm_resp = phy_isockets[route]->nb_transport_fw(transaction, phase, delay);

//   if (tlm_resp == TLM_UPDATED) {
//     wait(delay);
//   }

//   wait(phy_transaction_done);
// }

// void Interconnect::send_irq(tlm_generic_payload &transaction,
//                             tlm_command command) {
//   ChipletExtension *ext = nullptr;
//   tlm_phase phase = BEGIN_REQ;
//   sc_time delay = SC_ZERO_TIME;
//   tlm_sync_enum tlm_resp;

//   transaction.get_extension(ext);

//   unsigned int flit_data_size =
//   get_available_data_bytes_per_flit(transaction); unsigned int data_size =
//       (ext->flit_count * flit_data_size) - ext->flit_padding;

//   unsigned int address_offset = (ext->flit_count - 1) * flit_data_size;

//   auto *irq = new ChipletPayload();

//   irq->set_command(command);
//   irq->set_address(transaction.get_address() - address_offset);
//   irq->set_data_length(data_size);
//   irq->set_request_id(ext->request_id);
//   irq->set_core_id(ext->core_id);
//   irq->set_source_id(ext->source_id);
//   irq->set_destination_id(ext->destination_id);

//   if (command == TLM_READ_COMMAND) {
//     // send read IRQs to request core
//     SC_LOG_DEBUG(this, transaction, "Sending IRQ to Core" << ext->core_id);
//     tlm_resp = irq_sockets[ext->core_id]->nb_transport_fw(*irq, phase,
//     delay);
//   } else {
//     // send write IRQs to Core0
//     SC_LOG_DEBUG(this, transaction, "Sending IRQ to Core0");
//     tlm_resp = irq_sockets[0]->nb_transport_fw(*irq, phase, delay);
//   }

//   if (tlm_resp == TLM_COMPLETED) {
//     wait(delay);
//   }

//   SC_LOG_DEBUG(this, transaction, "Sending IRQ to Core done");

//   delete irq;
// }

// -------------------------------------------------------
// debug functions
// -------------------------------------------------------
void Interconnect::dump_staging_buffer() {
  std::cout << "=== Staging Buffer ===\n";
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

void Interconnect::dump_phy_buffers() {
  auto dump = [&](const std::string &name,
                  const std::vector<std::vector<uint8_t>> &buffers,
                  const std::vector<size_t> &ptrs) {
    std::cout << "=== " << name << " ===\n";
    for (size_t i = 0; i < buffers.size(); ++i) {
      std::cout << "Buffer[" << i << "] (used " << ptrs[i] << " / "
                << buffers[i].size() << " bytes):\n  ";
      for (size_t j = 0; j < ptrs[i]; ++j) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(buffers[i][j]) << " ";
        if ((j + 1) % flit_size == 0)
          std::cout << "\n  ";
      }
      std::cout << std::dec << "\n";
    }
  };

  dump("Tx Buffers", tx_buffers, tx_ptrs);
  dump("Rx Buffers", rx_buffers, rx_ptrs);
}