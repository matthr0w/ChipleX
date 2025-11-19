#include "modules/HWAccel.h"

#include "modules/DMAEngine.h"
#include "modules/chiplets/ChipletRegistry.h"

HWAccel::HWAccel(sc_module_name name, unsigned chiplet_id,
                 ChipletConfig chiplet_config, const CyclesDB &cycles,
                 DMAEngine *dma_engine)
    : sc_module(name), chiplet_id(chiplet_id),
      axi_width(chiplet_config.node["axi"]["width"].as<unsigned>()),
      cycles(cycles),
      clk_cycle(chiplet_config.node["cores"]["clk_cycle"].as<unsigned>(),
                SC_NS),
      dma_engine(dma_engine),
      tsocket("tsocket", *this, &HWAccel::nb_transport_fw,
              ARM::TLM::PROTOCOL_AXI4, axi_width) {
  if (dma_engine)
    dma_vm_id = dma_engine->register_virtual_initiator(this);

  MAX_INCR_BURST_SIZE = std::min(256 * axi_width / 8, 4096u);
  MAX_FIXED_BURST_SIZE = 16 * axi_width / 8;
  MAX_WRAP_BURST_SIZE = 16 * axi_width / 8;

  stats.register_utilization(this->name(), clk_cycle);

  SC_METHOD(clk_posedge);
  sensitive << clk.pos();
  dont_initialize();

  SC_METHOD(clk_negedge);
  sensitive << clk.neg();
  dont_initialize();

  SC_THREAD(main_thread);
}

void HWAccel::main_thread() {
  while (true) {
    wait(data_request);

    while (!data_queue.empty()) {
      ARM::AXI::Payload *transaction = data_queue.front();
      data_queue.pop_front();

      size_t len = transaction->get_data_length();
      uint8_t *buffer = new uint8_t[len];

      transaction->write_out(buffer);

      if (main_fn)
        main_fn(*this, buffer, len);

      delete[] buffer;
      state = AccelState::Idle;
    }
  }
}

void HWAccel::wait_cycles(const std::string &name) {
  stats.set_active(this->name());
  wait(cycles.get(name), SC_NS);
  stats.set_idle(this->name());
}

void HWAccel::clk_posedge() {
  if (aw_state == ACK) {
    aw_state = CLEAR;
    w_queue_out.push_back(aw_queue_out.front());
    aw_queue_out.pop_front();
  }

  if (w_state == ACK) {
    w_state = CLEAR;
    w_beat_count++;
    if (w_beat_count == w_queue_out.front()->get_beat_count()) {
      w_beat_count = 0;
      w_queue_out.pop_front();
      write_done.notify(SC_ZERO_TIME);
    }
  }

  if (b_state == ACK) {
    b_state = CLEAR;
    b_outgoing->unref();
    b_outgoing = nullptr;
  }

  if (ar_state == ACK) {
    ar_state = CLEAR;
    ar_queue_out.pop_front();
    read_done.notify(SC_ZERO_TIME);
  }

  if (!aw_queue_in.empty() && state == AccelState::Idle) {
    ARM::AXI::Phase phase = ARM::AXI::AW_READY;
    tsocket.nb_transport_bw(*aw_queue_in.front(), phase);
    state = AccelState::Busy;
  }

  switch (state) {
  case AccelState::Busy:
    if (!w_queue_in.empty()) {
      data_queue.push_back(w_queue_in.front());
      data_request.notify(clk_cycle);
      b_outgoing = w_queue_in.front();
      aw_queue_in.pop_front();
      w_queue_in.pop_front();
    }
  default:
    break;
  }
}

void HWAccel::clk_negedge() {
  // AW channel
  if (aw_state == CLEAR && !aw_queue_out.empty()) {
    ARM::AXI::Payload *payload = aw_queue_out.front();
    ARM::AXI::Phase phase = ARM::AXI::AW_VALID;

    aw_state = REQ;
    if (send_dma_request(*payload, ARM::AXI4::CHANNEL_AW))
      aw_state = ACK;
    else
      aw_state = CLEAR;
  }

  // W channel
  if (w_state == CLEAR && !w_queue_out.empty()) {
    ARM::AXI::Payload *payload = w_queue_out.front();
    ARM::AXI::Phase phase = (w_beat_count + 1 == payload->get_beat_count())
                                ? ARM::AXI::W_VALID_LAST
                                : ARM::AXI::W_VALID;

    w_state = REQ;
    if (send_dma_request(*payload, ARM::AXI4::CHANNEL_W))
      w_state = ACK;
    else
      w_state = CLEAR;
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
    ARM::AXI::Payload *payload = ar_queue_out.front();
    ARM::AXI::Phase phase = ARM::AXI::AR_VALID;

    ar_state = REQ;
    if (send_dma_request(*payload, ARM::AXI4::CHANNEL_AR))
      ar_state = ACK;
    else
      ar_state = CLEAR;
  }
}

// -------------------------------------------------------
// Transport Functions
// -------------------------------------------------------
tlm_sync_enum HWAccel::nb_transport_fw(ARM::AXI::Payload &payload,
                                       ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AW_VALID:
    aw_queue_in.push_back(&payload);
    payload.ref();
    return TLM_ACCEPTED;
  case ARM::AXI::W_VALID:
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  case ARM::AXI::W_VALID_LAST:
    w_queue_in.push_back(&payload);
    phase = ARM::AXI::W_READY;
    return TLM_UPDATED;
  case ARM::AXI::B_READY:
    b_state = b_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unexpected phase");
    return TLM_ACCEPTED;
  }
}

tlm_sync_enum HWAccel::nb_transport_bw_axi(ARM::AXI::Payload &payload,
                                           ARM::AXI::Phase &phase) {
  std::shared_ptr<RequestHandle> h = request_handles.find(&payload)->second;

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
    stats.increment_counter(this->name(), "transaction_count");
    stats.update_accum(this->name(), "transaction_total_latency_us",
                       (sc_time_stamp() - h->time_stamp).to_seconds() * 1e6);
    stats.update_minmax(this->name(), "transaction_latency_us",
                        (sc_time_stamp() - h->time_stamp).to_seconds() * 1e6);
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
    stats.increment_counter(this->name(), "transaction_count");
    stats.update_accum(this->name(), "transaction_total_latency_us",
                       (sc_time_stamp() - h->time_stamp).to_seconds() * 1e6);
    stats.update_minmax(this->name(), "transaction_latency_us",
                        (sc_time_stamp() - h->time_stamp).to_seconds() * 1e6);
    phase = ARM::AXI::B_READY;
    return TLM_UPDATED;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unrecognized phase");
    return TLM_ACCEPTED;
  }
}

// -------------------------------------------------------
// AXI Methods
// -------------------------------------------------------
std::shared_ptr<RequestHandle>
HWAccel::read_internal(uint32_t request_id, uint8_t src_module,
                       uint8_t dst_chiplet, uint8_t dst_module,
                       uint32_t address, bool fixed_address,
                       unsigned char *data, unsigned data_length,
                       ARM::AXI::Burst burst, bool is_volatile) {
  auto handle = std::make_shared<RequestHandle>();

  unsigned axi_bytes = axi_width / 8;
  unsigned beats = (data_length + axi_bytes - 1) / axi_bytes;
  uint8_t len = (beats > 0) ? (beats - 1) : 0;
  ARM::AXI::Size size = get_axi_size(axi_width);

  ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
      ARM::AXI::COMMAND_READ, address, size, len, burst);

  UserSignals user;
  user.src_chiplet = chiplet_id;
  user.src_module = src_module;
  user.dst_chiplet = dst_chiplet;
  user.dst_module = dst_module;
  user.fixed_address = fixed_address;

  payload->id = request_id;
  payload->user = user.encode();
  payload->cache = is_volatile ? ARM::AXI::CACHE_AR_DEVICE_NB
                               : ARM::AXI::CACHE_AR_WRITE_THROUGH_RWA;

  handle->payload = payload;
  handle->data = data;
  handle->time_stamp = sc_time_stamp();
  request_handles[payload] = handle;

  ar_queue_out.push_back(payload);

  wait(read_done);

  return handle;
}

std::shared_ptr<RequestHandle> HWAccel::read(const AxiRequest &req) {
  uint32_t max_burst = 0;
  switch (req.burst) {
  case ARM::AXI::BURST_FIXED:
    max_burst = MAX_FIXED_BURST_SIZE;
    break;
  case ARM::AXI::BURST_INCR:
    max_burst = MAX_INCR_BURST_SIZE;
    break;
  case ARM::AXI::BURST_WRAP:
    max_burst = MAX_WRAP_BURST_SIZE;
    break;
  default:
    SC_LOG_ERROR(this, "AXI Request Error: Unknown burst type");
  }

  SC_LOG_ASSERT(this, req.data_length <= max_burst,
                "AXI Request Error: Requested data length ("
                    << req.data_length
                    << " bytes) exceeds maximum allowed burst size ("
                    << max_burst << " bytes)");

  auto chiplet_desc = ChipletRegistry::instance().get(chiplet_id);
  std::string src_chiplet_name = chiplet_desc->chiplet_name;
  std::string dst_chiplet_name =
      req.dst_chiplet_name.value_or(src_chiplet_name);
  std::string src_module_name = req.src_module_name.value_or("memory");
  std::string dst_module_name = req.dst_module_name.value_or("memory");

  auto dst_chiplet_desc = ChipletRegistry::instance().get(dst_chiplet_name);

  SC_LOG_ASSERT(this, dst_chiplet_desc,
                "AXI Request Error: Chiplet " << dst_chiplet_name
                                              << " does not exist");

  uint8_t dst_chiplet_id = dst_chiplet_desc->chiplet_id;

  auto *src_module =
      ChipletRegistry::instance().get_module(chiplet_id, src_module_name);

  auto *dst_module =
      ChipletRegistry::instance().get_module(dst_chiplet_id, dst_module_name);

  SC_LOG_ASSERT(this, src_module,
                "AXI Request Error: Module " << src_module_name
                                             << " does not exist on "
                                             << src_chiplet_name);

  SC_LOG_ASSERT(this, dst_module,
                "AXI Request Error: Module " << dst_module_name
                                             << " does not exist on "
                                             << dst_chiplet_name);

  SC_LOG_ASSERT(this, src_module->is_subordinate(),
                "AXI Request Error: Module " << src_module_name
                                             << " is not an AXI subordinate");

  SC_LOG_ASSERT(this, dst_module->is_subordinate(),
                "AXI Request Error: Module " << dst_module_name
                                             << " is not an AXI subordinate");

  if (dst_chiplet_id == chiplet_id)
    SC_LOG_ASSERT(
        this, !src_module->is_interconnect(),
        "AXI Request Error: On-chip requests must not be forwarded to "
        "an interconnect");
  else
    SC_LOG_ASSERT(this, src_module->is_interconnect(),
                  "AXI Request Error: Off-chip requests must be forwarded to "
                  "an interconnect");

  SC_LOG_ASSERT(this, req.address.has_value(),
                "AXI Request Error: Read requests require an address");

  uint32_t address = req.address.value();
  bool fixed_address = true;
  bool is_volatile = req.is_volatile || (dst_chiplet_id != chiplet_id);

  return read_internal(req.request_id, src_module->id, dst_chiplet_id,
                       dst_module->id, address, fixed_address, req.data,
                       req.data_length, req.burst, is_volatile);
}

std::shared_ptr<RequestHandle>
HWAccel::write_internal(uint32_t request_id, uint8_t src_module,
                        uint8_t dst_chiplet, uint8_t dst_module,
                        uint32_t address, bool fixed_address,
                        unsigned char *data, unsigned data_length,
                        ARM::AXI::Burst burst, bool is_volatile) {
  auto handle = std::make_shared<RequestHandle>();

  unsigned axi_bytes = axi_width / 8;
  unsigned beats = (data_length + axi_bytes - 1) / axi_bytes;
  uint8_t len = (beats > 0) ? (beats - 1) : 0;
  ARM::AXI::Size size = get_axi_size(axi_width);

  ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
      ARM::AXI::COMMAND_WRITE, address, size, len, burst);

  UserSignals user;
  user.src_chiplet = chiplet_id;
  user.src_module = src_module;
  user.dst_chiplet = dst_chiplet;
  user.dst_module = dst_module;
  user.fixed_address = fixed_address;

  payload->id = request_id;
  payload->user = user.encode();
  payload->cache = is_volatile ? ARM::AXI::CACHE_AW_DEVICE_NB
                               : ARM::AXI::CACHE_AW_WRITE_THROUGH_RWA;

  payload->write_in(data);

  handle->payload = payload;
  handle->data = data;
  handle->time_stamp = sc_time_stamp();
  request_handles[payload] = handle;

  aw_queue_out.push_back(payload);

  wait(write_done);

  return handle;
}

std::shared_ptr<RequestHandle> HWAccel::write(const AxiRequest &req) {
  uint32_t max_burst = 0;
  switch (req.burst) {
  case ARM::AXI::BURST_FIXED:
    max_burst = MAX_FIXED_BURST_SIZE;
    break;
  case ARM::AXI::BURST_INCR:
    max_burst = MAX_INCR_BURST_SIZE;
    break;
  case ARM::AXI::BURST_WRAP:
    max_burst = MAX_WRAP_BURST_SIZE;
    break;
  default:
    SC_LOG_ERROR(this, "AXI Request Error: Unknown burst type");
  }

  SC_LOG_ASSERT(this, req.data_length <= max_burst,
                "AXI Request Error: Requested data length ("
                    << req.data_length
                    << " bytes) exceeds maximum allowed burst size ("
                    << max_burst << " bytes)");

  auto chiplet_desc = ChipletRegistry::instance().get(chiplet_id);
  std::string src_chiplet_name = chiplet_desc->chiplet_name;
  std::string dst_chiplet_name =
      req.dst_chiplet_name.value_or(src_chiplet_name);
  std::string src_module_name = req.src_module_name.value_or("memory");
  std::string dst_module_name = req.dst_module_name.value_or("memory");

  auto dst_chiplet_desc = ChipletRegistry::instance().get(dst_chiplet_name);

  SC_LOG_ASSERT(this, dst_chiplet_desc,
                "AXI Request Error: Chiplet " << dst_chiplet_name
                                              << " does not exist");

  uint8_t dst_chiplet_id = dst_chiplet_desc->chiplet_id;

  auto *src_module =
      ChipletRegistry::instance().get_module(chiplet_id, src_module_name);

  auto *dst_module =
      ChipletRegistry::instance().get_module(dst_chiplet_id, dst_module_name);

  SC_LOG_ASSERT(this, src_module,
                "AXI Request Error: Module " << src_module_name
                                             << " does not exist on "
                                             << src_chiplet_name);

  SC_LOG_ASSERT(this, dst_module,
                "AXI Request Error: Module " << dst_module_name
                                             << " does not exist on "
                                             << dst_chiplet_name);

  SC_LOG_ASSERT(this, src_module->is_subordinate(),
                "AXI Request Error: Module " << src_module_name
                                             << " is not an AXI subordinate");

  SC_LOG_ASSERT(this, dst_module->is_subordinate(),
                "AXI Request Error: Module " << dst_module_name
                                             << " is not an AXI subordinate");

  if (dst_chiplet_id == chiplet_id)
    SC_LOG_ASSERT(
        this, !src_module->is_interconnect(),
        "AXI Request Error: On-chip requests must not be forwarded to "
        "an interconnect");
  else
    SC_LOG_ASSERT(this, src_module->is_interconnect(),
                  "AXI Request Error: Off-chip requests must be forwarded to "
                  "an interconnect");

  uint32_t address = req.address.value_or(0x0);
  bool fixed_address = req.address.has_value();
  bool is_volatile =
      req.is_volatile || (dst_chiplet_id != chiplet_id) || !fixed_address;

  return write_internal(req.request_id, src_module->id, dst_chiplet_id,
                        dst_module->id, address, fixed_address, req.data,
                        req.data_length, req.burst, is_volatile);
}