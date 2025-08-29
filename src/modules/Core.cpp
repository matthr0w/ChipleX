#include "modules/Core.h"

Core::Core(sc_module_name name, unsigned int chip_id, unsigned int core_id,
           unsigned int axi_width, sc_time irq_delay)
    : sc_module(name), chip_id(chip_id), core_id(core_id), axi_width(axi_width),
      irq_delay(irq_delay), utilization_tracker(this->name()),
      isocket("isocket", *this, &Core::nb_transport_bw, ARM::TLM::PROTOCOL_AXI4,
              axi_width) {
  irq_socket.register_nb_transport_fw(this, &Core::nb_transport_fw_irq);

  MAX_INCR_BURST_SIZE = std::min(256 * axi_width / 8, 4096u);
  MAX_FIXED_BURST_SIZE = 16 * axi_width / 8;
  MAX_WRAP_BURST_SIZE = 16 * axi_width / 8;

  SC_METHOD(clock_posedge);
  sensitive << clock.pos();
  dont_initialize();

  SC_METHOD(clock_negedge);
  sensitive << clock.neg();
  dont_initialize();

  SC_THREAD(core_thread);
  SC_THREAD(interrupt_thread);
}

void Core::core_thread() {
  if (thread_fn) {
    thread_fn(*this, &utilization_tracker);
  }
}

void Core::interrupt_thread() {
  tlm_generic_payload *transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait(interrupt_request);

    while (!irq_queue.empty()) {
      transaction = irq_queue.front();
      irq_queue.pop_front();

      if (interrupt_fn) {
        interrupt_fn(*this, &utilization_tracker, transaction);
      }

      delete transaction;
    }
  }
}

void Core::clock_posedge() {
  if (ar_state == ACK)
    ar_state = CLEAR;

  if (aw_state == ACK)
    aw_state = CLEAR;

  if (w_state == ACK)
    w_state = CLEAR;
}

void Core::clock_negedge() {
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
        write_done.notify(SC_ZERO_TIME);
      }
    }
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum Core::nb_transport_fw_irq(tlm_generic_payload &transaction,
                                        tlm_phase &phase,
                                        sc_core::sc_time &delay) {
  switch (phase) {
  case BEGIN_REQ:
    delay += delays.irq_transfer(transaction);

    irq_queue.push_back(&transaction);
    interrupt_request.notify(delay);

    phase = END_REQ;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum Core::nb_transport_bw(ARM::AXI::Payload &payload,
                                    ARM::AXI::Phase &phase) {
  Core::RequestHandle *h = request_handles.find(&payload)->second;

  switch (phase) {
  case ARM::AXI::AR_READY:
    ar_state = ACK;
    return TLM_ACCEPTED;
  case ARM::AXI::R_VALID:
    phase = ARM::AXI::R_READY;
    return TLM_UPDATED;
  case ARM::AXI::R_VALID_LAST:
    request_handles.erase(&payload);
    h->notify(SC_ZERO_TIME);
    read_done.notify(SC_ZERO_TIME);
    phase = ARM::AXI::R_READY;
    return TLM_UPDATED;
  case ARM::AXI::AW_READY:
    aw_state = ACK;
    return TLM_ACCEPTED;
  case ARM::AXI::W_READY:
    w_state = ACK;
    return TLM_ACCEPTED;
  case ARM::AXI::B_VALID:
    request_handles.erase(&payload);
    h->notify(SC_ZERO_TIME);
    phase = ARM::AXI::B_READY;
    return TLM_UPDATED;
  default:
    SC_REPORT_ERROR(name(), "AXI TLM Protocol: Unrecognized phase");
    return TLM_ACCEPTED;
  }
}

// -------------------------------------------------------
// AXI methods
// -------------------------------------------------------
Core::RequestHandle *
Core::read_internal(uint32_t request_id, int destination_id, uint32_t address,
                    bool fixed_address, unsigned char *data,
                    unsigned int data_length, ARM::AXI::Burst burst,
                    bool is_volatile) {
  auto *handle = new RequestHandle();

  unsigned axi_bytes = axi_width / 8;
  unsigned beats = (data_length + axi_bytes - 1) / axi_bytes;
  uint8_t len = beats - 1;
  ARM::AXI::Size size = get_axi_size(axi_width);

  ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
      ARM::AXI::COMMAND_READ, address, size, len, burst);

  UserSignals user;
  user.core = core_id;
  user.source = chip_id;
  user.destination = destination_id;
  user.fixed_address = fixed_address;

  payload->id = request_id;
  payload->user = user.encode();
  payload->cache = is_volatile ? ARM::AXI::CACHE_AR_DEVICE_NB
                               : ARM::AXI::CACHE_AR_WRITE_THROUGH_RWA;

  SC_LOG_DEBUG_NO_TX(this,
                     "Sending request: READ from 0x" << std::hex << address);

  handle->payload = payload;
  handle->data = data;
  request_handles[payload] = handle;

  ar_queue.push_back(payload);

  wait(read_done);

  return handle;
}

Core::RequestHandle *Core::read(uint32_t request_id, int destination_id,
                                uint32_t address, unsigned char *data,
                                unsigned int data_length, bool is_volatile) {
  if (data_length > MAX_INCR_BURST_SIZE) {
    std::ostringstream oss;
    oss << "AXI Burst Error: Requested data length (" << data_length
        << " bytes) exceeds maximum allowed incremental burst size ("
        << MAX_INCR_BURST_SIZE << " bytes)";
    SC_REPORT_ERROR(name(), oss.str().c_str());
  }
  bool fixed_address = true;
  return read_internal(request_id, destination_id, address, fixed_address, data,
                       data_length, ARM::AXI::BURST_INCR, is_volatile);
}

Core::RequestHandle *Core::read_fixed(uint32_t request_id, int destination_id,
                                      uint32_t address, unsigned char *data,
                                      unsigned int data_length,
                                      bool is_volatile) {
  if (data_length > MAX_FIXED_BURST_SIZE) {
    std::ostringstream oss;
    oss << "AXI Burst Error: Requested data length (" << data_length
        << " bytes) exceeds maximum allowed fixed burst size ("
        << MAX_INCR_BURST_SIZE << " bytes)";
    SC_REPORT_ERROR(name(), oss.str().c_str());
  }
  bool fixed_address = true;
  return read_internal(request_id, destination_id, address, fixed_address, data,
                       data_length, ARM::AXI::BURST_FIXED, is_volatile);
}

Core::RequestHandle *Core::read_wrap(uint32_t request_id, int destination_id,
                                     uint32_t address, unsigned char *data,
                                     unsigned int data_length,
                                     bool is_volatile) {
  if (data_length > MAX_WRAP_BURST_SIZE) {
    std::ostringstream oss;
    oss << "AXI Burst Error: Requested data length (" << data_length
        << " bytes) exceeds maximum allowed wrap burst size ("
        << MAX_INCR_BURST_SIZE << " bytes)";
    SC_REPORT_ERROR(name(), oss.str().c_str());
  }
  bool fixed_address = true;
  return read_internal(request_id, destination_id, address, fixed_address, data,
                       data_length, ARM::AXI::BURST_WRAP, is_volatile);
}

Core::RequestHandle *
Core::write_internal(uint32_t request_id, int destination_id, uint32_t address,
                     bool fixed_address, unsigned char *data,
                     unsigned int data_length, ARM::AXI::Burst burst,
                     bool is_volatile) {
  auto *handle = new RequestHandle();

  unsigned axi_bytes = axi_width / 8;
  unsigned beats = (data_length + axi_bytes - 1) / axi_bytes;
  uint8_t len = beats - 1;
  ARM::AXI::Size size = get_axi_size(axi_width);

  ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
      ARM::AXI::COMMAND_WRITE, address, size, len, burst);

  UserSignals user;
  user.core = core_id;
  user.source = chip_id;
  user.destination = destination_id;
  user.fixed_address = fixed_address;

  payload->id = request_id;
  payload->user = user.encode();
  payload->cache = is_volatile ? ARM::AXI::CACHE_AW_DEVICE_NB
                               : ARM::AXI::CACHE_AW_WRITE_THROUGH_RWA;

  payload->write_in(data);

  SC_LOG_DEBUG_NO_TX(this,
                     "Sending request: Write to 0x" << std::hex << address);

  handle->payload = payload;
  handle->data = data;
  request_handles[payload] = handle;

  aw_queue.push_back(payload);

  wait(write_done);

  return handle;
}

Core::RequestHandle *Core::write(uint32_t request_id, int destination_id,
                                 uint32_t address, unsigned char *data,
                                 unsigned int data_length, bool is_volatile) {
  if (data_length > MAX_INCR_BURST_SIZE) {
    std::ostringstream oss;
    oss << "AXI Burst Error: Requested data length (" << data_length
        << " bytes) exceeds maximum allowed incremental burst size ("
        << MAX_INCR_BURST_SIZE << " bytes)";
    SC_REPORT_ERROR(name(), oss.str().c_str());
  }
  bool fixed_address = true;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, ARM::AXI::BURST_INCR, is_volatile);
}

Core::RequestHandle *Core::write(uint32_t request_id, int destination_id,
                                 unsigned char *data,
                                 unsigned int data_length) {
  if (data_length > MAX_INCR_BURST_SIZE) {
    std::ostringstream oss;
    oss << "AXI Burst Error: Requested data length (" << data_length
        << " bytes) exceeds maximum allowed incremental burst size ("
        << MAX_INCR_BURST_SIZE << " bytes)";
    SC_REPORT_ERROR(name(), oss.str().c_str());
  }
  uint32_t address = 0x0;
  bool fixed_address = false;
  bool is_volatile = true;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, ARM::AXI::BURST_INCR, is_volatile);
}

Core::RequestHandle *Core::write_fixed(uint32_t request_id, int destination_id,
                                       uint32_t address, unsigned char *data,
                                       unsigned int data_length,
                                       bool is_volatile) {
  if (data_length > MAX_INCR_BURST_SIZE) {
    std::ostringstream oss;
    oss << "AXI Burst Error: Requested data length (" << data_length
        << " bytes) exceeds maximum allowed fixed burst size ("
        << MAX_INCR_BURST_SIZE << " bytes)";
    SC_REPORT_ERROR(name(), oss.str().c_str());
  }
  bool fixed_address = true;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, ARM::AXI::BURST_FIXED, is_volatile);
}

Core::RequestHandle *Core::write_fixed(uint32_t request_id, int destination_id,
                                       unsigned char *data,
                                       unsigned int data_length) {
  if (data_length > MAX_INCR_BURST_SIZE) {
    std::ostringstream oss;
    oss << "AXI Burst Error: Requested data length (" << data_length
        << " bytes) exceeds maximum allowed fixed burst size ("
        << MAX_INCR_BURST_SIZE << " bytes)";
    SC_REPORT_ERROR(name(), oss.str().c_str());
  }
  uint32_t address = 0x0;
  bool fixed_address = false;
  bool is_volatile = true;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, ARM::AXI::BURST_FIXED, is_volatile);
}

Core::RequestHandle *Core::write_wrap(uint32_t request_id, int destination_id,
                                      uint32_t address, unsigned char *data,
                                      unsigned int data_length,
                                      bool is_volatile) {
  if (data_length > MAX_INCR_BURST_SIZE) {
    std::ostringstream oss;
    oss << "AXI Burst Error: Requested data length (" << data_length
        << " bytes) exceeds maximum allowed wrap burst size ("
        << MAX_INCR_BURST_SIZE << " bytes)";
    SC_REPORT_ERROR(name(), oss.str().c_str());
  }
  bool fixed_address = true;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, ARM::AXI::BURST_WRAP, is_volatile);
}

Core::RequestHandle *Core::write_wrap(uint32_t request_id, int destination_id,
                                      unsigned char *data,
                                      unsigned int data_length) {
  if (data_length > MAX_INCR_BURST_SIZE) {
    std::ostringstream oss;
    oss << "AXI Burst Error: Requested data length (" << data_length
        << " bytes) exceeds maximum allowed wrap burst size ("
        << MAX_INCR_BURST_SIZE << " bytes)";
    SC_REPORT_ERROR(name(), oss.str().c_str());
  }
  uint32_t address = 0x0;
  bool fixed_address = false;
  bool is_volatile = true;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, ARM::AXI::BURST_WRAP, is_volatile);
}