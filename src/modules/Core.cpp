#include "modules/Core.h"

#include "modules/DMAEngine.h"
#include "modules/chiplets/ChipletRegistry.h"

Core::Core(sc_module_name name, unsigned chiplet_id, unsigned core_id,
           YAML::Node config, const CyclesDB &cycles)
    : sc_module(name), chiplet_id(chiplet_id), core_id(core_id),
      axi_width(config["axi"]["width"].as<unsigned>()), cycles(cycles),
      clk_cycle(config["cores"]["clk_cycle"].as<unsigned>(), SC_NS),
      irq_delay(config["cores"]["irq_delay"].as<unsigned>(), SC_NS),
      isocket("isocket", *this, &Core::nb_transport_bw, ARM::TLM::PROTOCOL_AXI4,
              axi_width) {
  stats.register_utilization(this->name(), clk_cycle);

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

void Core::wait_cycles(const std::string &name) {
  stats.set_active(this->name());
  wait(cycles.get(name), SC_NS);
  stats.set_idle(this->name());
}

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
  // AW channel
  if (aw_state == CLEAR && !aw_queue.empty()) {
    ARM::AXI::Payload *payload = aw_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::AW_VALID;

    aw_state = REQ;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::AW_READY,
                    "AXI TLM Protocol: Unexpected phase");
      aw_state = ACK;
    }
  }

  // W channel
  if (w_state == CLEAR && !w_queue.empty()) {
    ARM::AXI::Payload *payload = w_queue.front();
    ARM::AXI::Phase phase = (w_beat_count + 1 == payload->get_beat_count())
                                ? ARM::AXI::W_VALID_LAST
                                : ARM::AXI::W_VALID;

    w_state = REQ;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::W_READY,
                    "AXI TLM Protocol: Unexpected phase");
      w_state = ACK;
    }
  }

  // AR channel
  if (ar_state == CLEAR && !ar_queue.empty()) {
    ARM::AXI::Payload *payload = ar_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::AR_VALID;

    ar_state = REQ;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::AR_READY,
                    "AXI TLM Protocol: Unexpected phase");
      ar_state = ACK;
    }
  }
}

// -------------------------------------------------------
// Transport Functions
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
std::shared_ptr<Core::RequestHandle>
Core::read_internal(uint32_t request_id, uint8_t src_module,
                    uint8_t dst_chiplet, uint8_t dst_module, uint32_t address,
                    bool fixed_address, unsigned char *data,
                    unsigned data_length, ARM::AXI::Burst burst,
                    bool is_volatile) {
  auto handle = std::make_shared<RequestHandle>();

  unsigned axi_bytes = axi_width / 8;
  unsigned beats = (data_length + axi_bytes - 1) / axi_bytes;
  uint8_t len = (beats > 0) ? (beats - 1) : 0;
  ARM::AXI::Size size = get_axi_size(axi_width);

  ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
      ARM::AXI::COMMAND_READ, address, size, len, burst);

  UserSignals user;
  user.core = core_id;
  user.src_chiplet = chiplet_id;
  user.src_module = src_module;
  user.dst_chiplet = dst_chiplet;
  user.dst_module = dst_module;
  user.fixed_address = fixed_address;

  payload->id = request_id;
  payload->user = user.encode();
  payload->cache = is_volatile ? ARM::AXI::CACHE_AR_DEVICE_NB
                               : ARM::AXI::CACHE_AR_WRITE_THROUGH_RWA;

  SC_LOG_INFO(this, "Sending request: READ from 0x" << std::hex << address);

  handle->payload = payload;
  handle->data = data;
  handle->time_stamp = sc_time_stamp();
  request_handles[payload] = handle;

  ar_queue.push_back(payload);

  wait(read_done);

  return handle;
}

std::shared_ptr<Core::RequestHandle> Core::read(const AxiRequest &req) {
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

std::shared_ptr<Core::RequestHandle>
Core::write_internal(uint32_t request_id, uint8_t src_module,
                     uint8_t dst_chiplet, uint8_t dst_module, uint32_t address,
                     bool fixed_address, unsigned char *data,
                     unsigned data_length, ARM::AXI::Burst burst,
                     bool is_volatile) {
  auto handle = std::make_shared<RequestHandle>();

  unsigned axi_bytes = axi_width / 8;
  unsigned beats = (data_length + axi_bytes - 1) / axi_bytes;
  uint8_t len = (beats > 0) ? (beats - 1) : 0;
  ARM::AXI::Size size = get_axi_size(axi_width);

  ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
      ARM::AXI::COMMAND_WRITE, address, size, len, burst);

  UserSignals user;
  user.core = core_id;
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

  SC_LOG_INFO(this, "Sending request: WRITE to 0x" << std::hex << address);

  handle->payload = payload;
  handle->data = data;
  handle->time_stamp = sc_time_stamp();
  request_handles[payload] = handle;

  aw_queue.push_back(payload);

  wait(write_done);

  return handle;
}

std::shared_ptr<Core::RequestHandle> Core::write(const AxiRequest &req) {
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

std::shared_ptr<Core::RequestHandle>
Core::dma_internal(uint32_t request_id, uint8_t src_module, uint8_t dst_chiplet,
                   uint8_t dst_module, uint8_t target_module,
                   uint32_t request_addr, uint32_t target_addr,
                   unsigned data_length, ARM::AXI::Burst burst,
                   bool is_volatile) {
  DMARequest req = {};

  req.request_id = request_id;
  req.core_id = core_id;

  // Read data from
  req.src_chiplet = chiplet_id;
  req.src_module = src_module;
  req.dst_chiplet = dst_chiplet;
  req.dst_module = dst_module;
  req.request_addr = request_addr;

  // Write data to
  req.target_module = target_module;
  req.target_addr = target_addr;

  req.data_length = data_length;
  req.burst = burst;
  req.is_volatile = is_volatile;

  auto chiplet_desc = ChipletRegistry::instance().get(chiplet_id);
  auto dma_engine = chiplet_desc->get("dma_engine");

  SC_LOG_ASSERT(this, dma_engine,
                "AXI DMA Request Error: Chiplet "
                    << chiplet_desc->chiplet_name
                    << " does not have a DMA engine");

  return write_internal(request_id, dma_engine->id, chiplet_id, dma_engine->id,
                        0, true, reinterpret_cast<unsigned char *>(&req),
                        sizeof(req), ARM::AXI::BURST_INCR, true);
}

std::shared_ptr<Core::RequestHandle> Core::dma(const AxiDMARequest &req) {
  SC_LOG_ASSERT(this, req.dst_chiplet_name.has_value(),
                "AXI DMA Request Error: Fetch chiplet not set. Did you call "
                "from() method?");
  SC_LOG_ASSERT(this, req.dst_module_name.has_value(),
                "AXI DMA Request Error: Fetch module not set. Did you call "
                "from() method?");
  SC_LOG_ASSERT(this, req.request_addr.has_value(),
                "AXI DMA Request Error: Fetch address not set. Did you call "
                "from() method?");
  SC_LOG_ASSERT(this, req.target_module_name.has_value(),
                "AXI DMA Request Error: Target module not set. Did you call "
                "to() method?");
  SC_LOG_ASSERT(this, req.target_addr.has_value(),
                "AXI DMA Request Error: Target address not set. Did you call "
                "to() method?");

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
    SC_LOG_ERROR(this, "AXI DMA Request Error: Unknown burst type");
  }

  SC_LOG_ASSERT(this, req.data_length <= max_burst,
                "AXI DMA Request Error: Requested data length ("
                    << req.data_length
                    << " bytes) exceeds maximum allowed burst size ("
                    << max_burst << " bytes)");

  auto chiplet_desc = ChipletRegistry::instance().get(chiplet_id);
  std::string src_chiplet_name = chiplet_desc->chiplet_name;
  std::string dst_chiplet_name = req.dst_chiplet_name.value();
  std::string dst_module_name = req.dst_module_name.value();

  auto dst_chiplet_desc = ChipletRegistry::instance().get(dst_chiplet_name);

  SC_LOG_ASSERT(this, dst_chiplet_desc,
                "AXI Request Error: Chiplet " << dst_chiplet_name
                                              << " does not exist");

  uint8_t dst_chiplet_id = dst_chiplet_desc->chiplet_id;

  if (dst_chiplet_id != chiplet_id)
    SC_LOG_ASSERT(this, req.src_module_name.has_value(),
                  "AXI DMA Request Error: Via module not set. Did you call "
                  "via() method?");

  std::string src_module_name = req.src_module_name.value();
  std::string target_module_name = req.target_module_name.value();

  auto *src_module =
      ChipletRegistry::instance().get_module(chiplet_id, src_module_name);

  auto *dst_module =
      ChipletRegistry::instance().get_module(dst_chiplet_id, dst_module_name);

  auto *target_module = ChipletRegistry::instance().get_module(
      dst_chiplet_id, target_module_name);

  SC_LOG_ASSERT(this, src_module,
                "AXI DMA Request Error: Module " << src_module_name
                                                 << " does not exist on "
                                                 << src_chiplet_name);

  SC_LOG_ASSERT(this, dst_module,
                "AXI DMA Request Error: Module " << dst_module_name
                                                 << " does not exist on "
                                                 << dst_chiplet_name);

  SC_LOG_ASSERT(this, src_module->is_subordinate(),
                "AXI DMA Request Error: Module "
                    << src_module_name << " is not an AXI subordinate");

  SC_LOG_ASSERT(this, dst_module->is_subordinate(),
                "AXI DMA Request Error: Module "
                    << dst_module_name << " is not an AXI subordinate");

  if (dst_chiplet_id == chiplet_id)
    SC_LOG_ASSERT(
        this, !src_module->is_interconnect(),
        "AXI DMA Request Error: On-chip requests must not be forwarded to "
        "an interconnect");
  else
    SC_LOG_ASSERT(
        this, src_module->is_interconnect(),
        "AXI DMA Request Error: Off-chip requests must be forwarded to "
        "an interconnect");

  bool is_volatile = req.is_volatile || (dst_chiplet_id != chiplet_id);

  return dma_internal(req.request_id, src_module->id, dst_chiplet_id,
                      dst_module->id, target_module->id,
                      req.request_addr.value(), req.target_addr.value(),
                      req.data_length, req.burst, is_volatile);
}