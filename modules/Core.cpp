#include "Core.h"

Core::Core(sc_module_name name, unsigned int chip_id, unsigned int core_id,
           sc_time irq_delay)
    : sc_module(name), chip_id(chip_id), core_id(core_id), irq_delay(irq_delay),
      utilization_tracker(this->name()), aw_state(CLEAR), w_state(CLEAR),
      ar_state(CLEAR), w_beat_count(0),
      isocket("initiator", *this, &Core::nb_transport_bw,
              ARM::TLM::PROTOCOL_AXI4, 32),
      clock("clock") {
  // irq_socket.register_nb_transport_fw(this, &Core::nb_transport_fw_irq);

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
    wait(irq_req_evt);

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
  if (aw_state == ACK)
    aw_state = CLEAR;

  if (w_state == ACK)
    w_state = CLEAR;

  if (ar_state == ACK)
    ar_state = CLEAR;
}

void Core::clock_negedge() {
  /* Send next payload AWVALID */
  if (aw_state == CLEAR && !aw_queue.empty()) {
    ARM::AXI::Payload *payload = aw_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::AW_VALID;

    w_queue.push_back(payload);
    aw_queue.pop_front();

    aw_state = REQ;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::AW_READY);
      aw_state = ACK;
    }
  }

  /* Send next payload ARVALID */
  if (ar_state == CLEAR && !ar_queue.empty()) {
    ARM::AXI::Payload *payload = ar_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::AR_VALID;

    ar_queue.pop_front();

    ar_state = REQ;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::AR_READY);
      ar_state = ACK;
    }
  }

  /* Send write beat WVALID */
  if (w_state == CLEAR && !w_queue.empty()) {
    ARM::AXI::Payload *payload = w_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::W_VALID;

    w_beat_count++;

    if (w_beat_count == payload->get_beat_count()) {
      phase = ARM::AXI::W_VALID_LAST;
      w_queue.pop_front();
      w_beat_count = 0;
      write_done.notify(SC_ZERO_TIME);
    }

    w_state = REQ;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::W_READY);
      w_state = ACK;
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

    // auto *transaction_copy =
    // static_cast<ChipletPayload *>(&transaction)->clone();

    irq_queue.push_back(&transaction);
    irq_req_evt.notify(delay);

    phase = END_REQ;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum Core::nb_transport_bw(ARM::AXI::Payload &payload,
                                    ARM::AXI::Phase &phase) {
  SC_LOG_DEBUG_NO_TX(this, "AXI TLM Protocol: " << phase_to_string(phase));

  Core::RequestHandle *h = request_handles.find(&payload)->second;

  switch (phase) {
  case ARM::AXI::AW_READY:
    aw_state = ACK;
    return TLM_ACCEPTED;
  case ARM::AXI::W_READY:
    w_state = ACK;
    return TLM_ACCEPTED;
  case ARM::AXI::B_VALID:
    phase = ARM::AXI::B_READY;
    request_handles.erase(&payload);
    h->notify(SC_ZERO_TIME);
    return TLM_UPDATED;
  case ARM::AXI::AR_READY:
    ar_state = ACK;
    return TLM_ACCEPTED;
  case ARM::AXI::R_VALID:
    phase = ARM::AXI::R_READY;
    return TLM_UPDATED;
  case ARM::AXI::R_VALID_LAST:
    read_done.notify(SC_ZERO_TIME);
    return TLM_ACCEPTED;
  default:
    SC_REPORT_ERROR(name(), "AXI TLM Protocol: Unrecognized phase");
    return TLM_ACCEPTED;
  }
}

// -------------------------------------------------------
// AXI methods
// -------------------------------------------------------
Core::RequestHandle *Core::read_internal(int request_id, int destination_id,
                                         uint32_t address, bool fixed_address,
                                         unsigned char *data,
                                         unsigned int data_length,
                                         unsigned int burst_type,
                                         bool is_volatile) {
  auto *handle = new RequestHandle();

  unsigned int beats = (data_length + 4 - 1) / 4;
  uint8_t len = beats - 1;
  ARM::AXI::Size size = ARM::AXI::SIZE_4;
  ARM::AXI::Burst burst = ARM::AXI::BURST_INCR;

  switch (burst_type) {
  case 0:
    burst = ARM::AXI::BURST_FIXED;
    break;
  case 1:
    burst = ARM::AXI::BURST_INCR;
    break;
  case 2:
    burst = ARM::AXI::BURST_WRAP;
    break;
  }

  ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
      ARM::AXI::COMMAND_READ, address, size, len, burst);

  SC_LOG_DEBUG_NO_TX(this,
                     "Sending request: READ from 0x" << std::hex << address);

  handle->payload = payload;
  request_handles[payload] = handle;

  ar_queue.push_back(payload);

  wait(read_done);

  return handle;
}

Core::RequestHandle *Core::read(int request_id, int destination_id,
                                uint32_t address, unsigned char *data,
                                unsigned int data_length, bool is_volatile) {
  bool fixed_address = true;
  unsigned int burst_type = 1;
  return read_internal(request_id, destination_id, address, fixed_address, data,
                       data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::read_fixed(int request_id, int destination_id,
                                      uint32_t address, unsigned char *data,
                                      unsigned int data_length,
                                      bool is_volatile) {
  bool fixed_address = true;
  unsigned int burst_type = 0;
  return read_internal(request_id, destination_id, address, fixed_address, data,
                       data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::read_wrap(int request_id, int destination_id,
                                     uint32_t address, unsigned char *data,
                                     unsigned int data_length,
                                     bool is_volatile) {
  bool fixed_address = true;
  unsigned int burst_type = 2;
  return read_internal(request_id, destination_id, address, fixed_address, data,
                       data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::write_internal(int request_id, int destination_id,
                                          uint32_t address, bool fixed_address,
                                          unsigned char *data,
                                          unsigned int data_length,
                                          unsigned int burst_type,
                                          bool is_volatile) {
  auto *handle = new RequestHandle();

  unsigned int beats = (data_length + 4 - 1) / 4;
  uint8_t len = beats - 1;
  ARM::AXI::Size size = ARM::AXI::SIZE_4;
  ARM::AXI::Burst burst = ARM::AXI::BURST_INCR;

  switch (burst_type) {
  case 0:
    burst = ARM::AXI::BURST_FIXED;
    break;
  case 1:
    burst = ARM::AXI::BURST_INCR;
    break;
  case 2:
    burst = ARM::AXI::BURST_WRAP;
    break;
  }

  ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
      ARM::AXI::COMMAND_WRITE, address, size, len, burst);

  SC_LOG_DEBUG_NO_TX(this,
                     "Sending request: Write from 0x" << std::hex << address);

  handle->payload = payload;
  request_handles[payload] = handle;

  aw_queue.push_back(payload);

  wait(write_done);

  return handle;
}

Core::RequestHandle *Core::write(int request_id, int destination_id,
                                 uint32_t address, unsigned char *data,
                                 unsigned int data_length, bool is_volatile) {
  bool fixed_address = true;
  unsigned int burst_type = 1;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::write(int request_id, int destination_id,
                                 unsigned char *data,
                                 unsigned int data_length) {
  uint32_t address = 0x0;
  bool fixed_address = false;
  unsigned int burst_type = 1;
  bool is_volatile = true;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::write_fixed(int request_id, int destination_id,
                                       uint32_t address, unsigned char *data,
                                       unsigned int data_length,
                                       bool is_volatile) {
  bool fixed_address = true;
  unsigned int burst_type = 0;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::write_fixed(int request_id, int destination_id,
                                       unsigned char *data,
                                       unsigned int data_length) {
  uint32_t address = 0x0;
  bool fixed_address = false;
  unsigned int burst_type = 0;
  bool is_volatile = true;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::write_wrap(int request_id, int destination_id,
                                      uint32_t address, unsigned char *data,
                                      unsigned int data_length,
                                      bool is_volatile) {
  bool fixed_address = true;
  unsigned int burst_type = 2;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::write_wrap(int request_id, int destination_id,
                                      unsigned char *data,
                                      unsigned int data_length) {
  uint32_t address = 0x0;
  bool fixed_address = false;
  unsigned int burst_type = 2;
  bool is_volatile = true;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, burst_type, is_volatile);
}