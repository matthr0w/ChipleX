#include "modules/Core.h"

Core::Core(sc_module_name name, unsigned chiplet_id, unsigned core_id,
           YAML::Node config)
    : sc_module(name), chiplet_id(chiplet_id), core_id(core_id),
      axi_width(config["axi"]["width"].as<unsigned>()),
      clk_cycle(config["cores"]["clk_cycle"].as<unsigned>(), SC_NS),
      irq_delay(config["cores"]["irq_delay"].as<unsigned>(), SC_NS),
      isocket("isocket", *this, &Core::nb_transport_bw, ARM::TLM::PROTOCOL_AXI4,
              axi_width) {
  irq_socket.register_nb_transport_fw(this, &Core::nb_transport_fw_irq);

  MAX_INCR_BURST_SIZE = std::min(256 * axi_width / 8, 4096u);
  MAX_FIXED_BURST_SIZE = 16 * axi_width / 8;
  MAX_WRAP_BURST_SIZE = 16 * axi_width / 8;

  SC_METHOD(clk_posedge);
  sensitive << clk.pos();
  dont_initialize();

  SC_METHOD(clk_negedge);
  sensitive << clk.neg();
  dont_initialize();

  SC_THREAD(core_thread);
  SC_THREAD(interrupt_thread);
}

void Core::core_thread() {
  if (thread_fn)
    thread_fn(*this);
}

void Core::interrupt_thread() {
  while (true) {
    wait(interrupt_request);

    while (!irq_queue.empty()) {
      tlm_generic_payload *transaction = irq_queue.front();
      irq_queue.pop_front();

      if (interrupt_fn)
        interrupt_fn(*this, transaction);

      delete transaction;
    }
  }
}

void Core::wait_cycles(unsigned count) { wait(count * clk_cycle); }

void Core::clk_posedge() {
  if (aw_state == ACK) {
    aw_state = CLEAR;
    w_queue.push_back(aw_queue.front());
    aw_queue.pop_front();
  }

  if (w_state == ACK) {
    w_state = CLEAR;
    w_beat_count++;
    if (w_beat_count == w_queue.front()->get_beat_count()) {
      w_beat_count = 0;
      write_done.notify(SC_ZERO_TIME);
      w_queue.pop_front();
    }
  }

  if (ar_state == ACK) {
    ar_state = CLEAR;
    read_done.notify(SC_ZERO_TIME);
    ar_queue.pop_front();
  }
}

void Core::clk_negedge() {
  /* Send next payload AWVALID */
  if (aw_state == CLEAR && !aw_queue.empty()) {
    ARM::AXI::Payload *payload = aw_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::AW_VALID;

    aw_state = REQ;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::AW_READY);
      aw_state = ACK;
    }
  }

  /* Send write beat WVALID */
  if (w_state == CLEAR && !w_queue.empty()) {
    ARM::AXI::Payload *payload = w_queue.front();
    ARM::AXI::Phase phase = (w_beat_count + 1 == payload->get_beat_count())
                                ? ARM::AXI::W_VALID_LAST
                                : ARM::AXI::W_VALID;

    w_state = REQ;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::W_READY);
      w_state = ACK;
    }
  }

  /* Send next payload ARVALID */
  if (ar_state == CLEAR && !ar_queue.empty()) {
    ARM::AXI::Payload *payload = ar_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::AR_VALID;

    ar_state = REQ;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::AR_READY);
      ar_state = ACK;
    }
  }
}

// -------------------------------------------------------
// Transport functions
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
    ar_state = ar_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::R_VALID:
    phase = ARM::AXI::R_READY;
    return TLM_UPDATED;
  case ARM::AXI::R_VALID_LAST:
    request_handles.erase(&payload);
    h->notify(SC_ZERO_TIME);
    phase = ARM::AXI::R_READY;
    return TLM_UPDATED;
  case ARM::AXI::AW_READY:
    aw_state = aw_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::W_READY:
    w_state = w_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::B_VALID:
    request_handles.erase(&payload);
    h->notify(SC_ZERO_TIME);
    phase = ARM::AXI::B_READY;
    return TLM_UPDATED;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unrecognized phase");
    return TLM_ACCEPTED;
  }
}

// -------------------------------------------------------
// AXI methods
// -------------------------------------------------------
Core::RequestHandle *
Core::read_internal(uint32_t request_id, uint8_t destination_id,
                    uint32_t address, bool fixed_address, unsigned char *data,
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
  user.source = chiplet_id;
  user.destination = destination_id;
  user.fixed_address = fixed_address;

  payload->id = request_id;
  payload->user = user.encode();
  payload->cache = is_volatile ? ARM::AXI::CACHE_AR_DEVICE_NB
                               : ARM::AXI::CACHE_AR_WRITE_THROUGH_RWA;

  SC_LOG_INFO(this, "Sending request: READ from 0x" << std::hex << address);

  handle->payload = payload;
  handle->data = data;
  request_handles[payload] = handle;

  ar_queue.push_back(payload);

  wait(read_done);

  return handle;
}

Core::RequestHandle *Core::read(const ReadRequest &req) {
  switch (req.burst) {
  case ARM::AXI::BURST_FIXED:
    if (req.data_length > MAX_FIXED_BURST_SIZE) {
      std::ostringstream message;
      message << "AXI Burst Error: Requested data length (" << req.data_length
              << " bytes) exceeds maximum allowed fixed burst size ("
              << MAX_FIXED_BURST_SIZE << " bytes)";
      SC_LOG_ERROR(this, message.str().c_str());
    }
    break;
  case ARM::AXI::BURST_INCR:
    if (req.data_length > MAX_INCR_BURST_SIZE) {
      std::ostringstream message;
      message << "AXI Burst Error: Requested data length (" << req.data_length
              << " bytes) exceeds maximum allowed incremental burst size ("
              << MAX_INCR_BURST_SIZE << " bytes)";
      SC_LOG_ERROR(this, message.str().c_str());
    }
    break;
  case ARM::AXI::BURST_WRAP:
    if (req.data_length > MAX_WRAP_BURST_SIZE) {
      std::ostringstream message;
      message << "AXI Burst Error: Requested data length (" << req.data_length
              << " bytes) exceeds maximum allowed wrap burst size ("
              << MAX_WRAP_BURST_SIZE << " bytes)";
      SC_LOG_ERROR(this, message.str().c_str());
    }
    break;
  default:
    SC_LOG_ERROR(this, "AXI Burst Error: Unknown burst type");
  }

  uint8_t destination_id = req.destination_id.value_or(chiplet_id);
  bool fixed_address = true;
  bool is_volatile = req.is_volatile || (destination_id != chiplet_id);

  return read_internal(req.request_id, destination_id, req.address,
                       fixed_address, req.data, req.data_length, req.burst,
                       is_volatile);
}

Core::RequestHandle *
Core::write_internal(uint32_t request_id, uint8_t destination_id,
                     uint32_t address, bool fixed_address, unsigned char *data,
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
  user.source = chiplet_id;
  user.destination = destination_id;
  user.fixed_address = fixed_address;

  payload->id = request_id;
  payload->user = user.encode();
  payload->cache = is_volatile ? ARM::AXI::CACHE_AW_DEVICE_NB
                               : ARM::AXI::CACHE_AW_WRITE_THROUGH_RWA;

  payload->write_in(data);

  SC_LOG_INFO(this, "Sending request: WRITE to 0x" << std::hex << address);

  handle->payload = payload;
  handle->data = data;
  request_handles[payload] = handle;

  aw_queue.push_back(payload);

  wait(write_done);

  return handle;
}

Core::RequestHandle *Core::write(const WriteRequest &req) {
  switch (req.burst) {
  case ARM::AXI::BURST_FIXED:
    if (req.data_length > MAX_FIXED_BURST_SIZE) {
      std::ostringstream message;
      message << "AXI Burst Error: Requested data length (" << req.data_length
              << " bytes) exceeds maximum allowed fixed burst size ("
              << MAX_FIXED_BURST_SIZE << " bytes)";
      SC_LOG_ERROR(this, message.str().c_str());
    }
    break;
  case ARM::AXI::BURST_INCR:
    if (req.data_length > MAX_INCR_BURST_SIZE) {
      std::ostringstream message;
      message << "AXI Burst Error: Requested data length (" << req.data_length
              << " bytes) exceeds maximum allowed incremental burst size ("
              << MAX_INCR_BURST_SIZE << " bytes)";
      SC_LOG_ERROR(this, message.str().c_str());
    }
    break;
  case ARM::AXI::BURST_WRAP:
    if (req.data_length > MAX_WRAP_BURST_SIZE) {
      std::ostringstream message;
      message << "AXI Burst Error: Requested data length (" << req.data_length
              << " bytes) exceeds maximum allowed wrap burst size ("
              << MAX_WRAP_BURST_SIZE << " bytes)";
      SC_LOG_ERROR(this, message.str().c_str());
    }
    break;
  default:
    SC_LOG_ERROR(this, "AXI Burst Error: Unknown burst type");
  }

  uint8_t destination_id = req.destination_id.value_or(chiplet_id);
  uint32_t address = req.address.value_or(0x0);
  bool fixed_address = req.address.has_value();
  bool is_volatile =
      req.is_volatile || (destination_id != chiplet_id) || !fixed_address;

  return write_internal(req.request_id, destination_id, address, fixed_address,
                        req.data, req.data_length, req.burst, is_volatile);
}