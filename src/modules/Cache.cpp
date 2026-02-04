#include "modules/Cache.h"

#include "logging.h"

#include "modules/chiplets/ChipletRegistry.h"

// TODO: Fix zero and one store buffer entries behavior

Cache::Cache(sc_module_name name, unsigned chiplet_id,
             ChipletConfig chiplet_config)
    : sc_module(name), chiplet_id(chiplet_id),
      axi_width(chiplet_config.node["axi"]["width"].as<unsigned>()),
      cache_size(chiplet_config.node["caches"]["size"].as<unsigned>()),
      cache_block_size(
          chiplet_config.node["caches"]["block_size"].as<unsigned>()),
      cache_store_buffer_size(
          chiplet_config.node["caches"]["store_buffer_size"].as<unsigned>()),
      clk_cycle(chiplet_config.node["caches"]["clk_cycle"].as<unsigned>(),
                SC_NS),
      beat_data(new uint8_t[axi_width >> 3]),
      tsocket("tsocket", *this, &Cache::nb_transport_fw,
              ARM::TLM::PROTOCOL_AXI4, axi_width),
      isocket("isocket", *this, &Cache::nb_transport_bw,
              ARM::TLM::PROTOCOL_AXI4, axi_width) {
  // Assertions
  LOG_ASSERT(
      cache_size % cache_block_size == 0,
      "Parameter Error: Cache size must be a multiple of cache block size");

  stats.register_utilization(this->name(), clk_cycle);

  num_lines = cache_size / cache_block_size;
  cache_lines.resize(num_lines);
  store_buffer.resize(cache_store_buffer_size);

  for (auto &line : cache_lines) {
    line.valid = false;
    line.tag = 0;
    line.data.resize(cache_block_size, 0);
  }

  num_accesses = 0;
  num_hits = 0;
  num_misses = 0;

  SC_METHOD(clk_posedge);
  sensitive << clk.pos();
  dont_initialize();

  SC_METHOD(clk_negedge);
  sensitive << clk.neg();
  dont_initialize();
}

Cache::~Cache() { delete[] beat_data; }

void Cache::end_of_simulation() {
  double hit_rate =
      num_accesses > 0 ? double(100) * num_hits / num_accesses : 0;
  double miss_rate =
      num_accesses > 0 ? double(100) * num_misses / num_accesses : 0;
  stats.set_value(this->name(), "hit_rate", hit_rate);
  stats.set_value(this->name(), "miss_rate", miss_rate);
}

void Cache::clk_posedge() {
  if (aw_state == ACK) {
    aw_state = CLEAR;
    w_queue_out.push_back(aw_queue_out.front());
    aw_queue_out.pop_front();
  }

  if (w_state == ACK) {
    w_state = CLEAR;
    w_beat_count_out++;
    if (w_beat_count_out == w_queue_out.front()->get_beat_count()) {
      w_beat_count_out = 0;
      w_queue_out.pop_front();
    }
  }

  if (ar_state == ACK) {
    ar_state = CLEAR;
    ar_queue_out.pop_front();
  }

  // Write request
  if (!b_outgoing && !aw_queue_in.empty()) {
    b_outgoing = aw_queue_in.front();
    aw_queue_in.pop_front();
    w_beat_count_in = 0;

    ARM::AXI::Phase phase = ARM::AXI::AW_READY;
    tsocket.nb_transport_bw(*b_outgoing, phase);
  }

  if (b_outgoing && !w_queue_in.empty()) {
    size_t next_tail = (store_buffer_tail + 1) % store_buffer.size();

    // Check if store buffer is full
    if (next_tail != store_buffer_head) {
      uint32_t beat_addr = set_beat_address(
          b_outgoing->get_address(), w_beat_count_in,
          b_outgoing->get_beat_data_length(), b_outgoing->get_beat_count(),
          b_outgoing->get_burst());

      // Move into store buffer
      StoreBufferEntry &sbe = store_buffer[store_buffer_tail];
      sbe.address = beat_addr;
      sbe.data.resize(b_outgoing->get_beat_data_length());
      b_outgoing->write_out_beat(w_beat_count_in, sbe.data.data());
      sbe.valid = true;

      store_buffer_tail = next_tail;

      // Overwrite cache if valid
      unsigned remaining = b_outgoing->get_beat_data_length();
      unsigned beat_offset = 0;

      while (remaining > 0) {
        num_accesses++;

        uint32_t block_address = beat_addr & ~(cache_block_size - 1);
        uint32_t block_offset = beat_addr & (cache_block_size - 1);

        uint32_t tag = block_address >>
                       ((unsigned)(log2(cache_block_size) + log2(num_lines)));
        uint32_t index =
            (block_address >> (unsigned)log2(cache_block_size)) % num_lines;

        uint32_t copy_len =
            std::min(cache_block_size - block_offset, remaining);

        CacheLine &line = cache_lines[index];

        if (!(line.valid && line.tag == tag)) {
          // Write Miss
          SC_LOG_DEBUG(this, "Write Miss: Writing data only to store buffer");
          num_misses++;
        } else {
          // Write Hit
          SC_LOG_DEBUG(this,
                       "Write Hit: Writing data to store buffer and cache");
          num_hits++;

          std::memcpy(&line.data[block_offset], sbe.data.data() + beat_offset,
                      copy_len);
        }

        beat_addr += copy_len;
        beat_offset += copy_len;
        remaining -= copy_len;
      }

      w_queue_in.pop_front();
      w_beat_count_in++;

      ARM::AXI::Phase phase = ARM::AXI::W_READY;
      tsocket.nb_transport_bw(*b_outgoing, phase);

      if (w_beat_count_in >= b_outgoing->get_beat_count())
        b_beat_ready = true;
    }

    stats.mark_active_cycle(this->name(), 0.5);
  }

  // Read request
  if (!r_outgoing && !ar_queue_in.empty()) {
    r_outgoing = ar_queue_in.front();
    ar_queue_in.pop_front();
    r_beat_count = 0;

    ARM::AXI::Phase phase = ARM::AXI::AR_READY;
    tsocket.nb_transport_bw(*r_outgoing, phase);
  }

  if (r_outgoing && active_cache_line_requests.empty()) {
    uint32_t beat_addr =
        set_beat_address(r_outgoing->get_address(), r_beat_count,
                         r_outgoing->get_beat_data_length(),
                         r_outgoing->get_beat_count(), r_outgoing->get_burst());

    // Read cache if valid
    unsigned remaining = r_outgoing->get_beat_data_length();
    unsigned beat_offset = 0;

    r_beat_ready = true;

    while (remaining > 0) {
      num_accesses++;

      uint32_t block_address = beat_addr & ~(cache_block_size - 1);
      uint32_t block_offset = beat_addr & (cache_block_size - 1);

      uint32_t tag = block_address >>
                     ((unsigned)(log2(cache_block_size) + log2(num_lines)));
      uint32_t index =
          (block_address >> (unsigned)log2(cache_block_size)) % num_lines;

      uint32_t copy_len = std::min(cache_block_size - block_offset, remaining);

      CacheLine &line = cache_lines[index];

      if (!(line.valid && line.tag == tag)) {
        // Read Miss: enqueue line fetch
        SC_LOG_DEBUG(this, "Read Miss: Enqueuing line fetch from address 0x"
                               << std::hex << block_address);
        num_misses++;

        r_beat_ready = false;

        CacheLineRequest clr;
        clr.line = &line;
        clr.address = block_address;
        clr.tag = tag;
        cache_line_requests.push_back(clr);
      } else {
        // Read Hit
        SC_LOG_DEBUG(this, "Read Hit: Loading data from cache");
        num_hits++;

        std::memcpy(beat_data + beat_offset, &line.data[block_offset],
                    copy_len);
      }

      beat_addr += copy_len;
      beat_offset += copy_len;
      remaining -= copy_len;
    }

    if (r_beat_ready)
      r_outgoing->read_in_beat(beat_data);

    stats.mark_active_cycle(this->name(), 0.5);
  }
}

void Cache::clk_negedge() {
  if (!cache_line_requests.empty()) {
    enqueue_cacheline_read();
    stats.mark_active_cycle(this->name(), 0.5);
  }

  if (store_buffer_head != store_buffer_tail) {
    enqueue_storebuffer_write();
    stats.mark_active_cycle(this->name(), 0.5);
  }

  // AW channel
  if (aw_state == CLEAR && !aw_queue_out.empty()) {
    ARM::AXI::Payload *payload = aw_queue_out.front();
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
  if (w_state == CLEAR && !w_queue_out.empty()) {
    ARM::AXI::Payload *payload = w_queue_out.front();
    ARM::AXI::Phase phase = (w_beat_count_out + 1 == payload->get_beat_count())
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
  if (ar_state == CLEAR && !ar_queue_out.empty()) {
    ARM::AXI::Payload *payload = ar_queue_out.front();
    ARM::AXI::Phase phase = ARM::AXI::AR_VALID;

    ar_state = REQ;
    tlm_sync_enum reply = isocket.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::AR_READY,
                    "AXI TLM Protocol: Unexpected phase");
      ar_state = ACK;
    }
  }

  // R channel
  if (r_beat_ready) {
    ARM::AXI::Phase phase = (r_beat_count + 1 == r_outgoing->get_beat_count())
                                ? ARM::AXI::R_VALID_LAST
                                : ARM::AXI::R_VALID;

    tlm_sync_enum reply = tsocket.nb_transport_bw(*r_outgoing, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::R_READY,
                    "AXI TLM Protocol: Unexpected phase");
      if (r_beat_count + 1 == r_outgoing->get_beat_count()) {
        r_outgoing->unref();
        r_outgoing = nullptr;
        r_beat_ready = false;
      } else {
        r_beat_count++;
      }
    }
  }

  // B channel
  if (b_beat_ready) {
    ARM::AXI::Phase phase = ARM::AXI::B_VALID;

    tlm_sync_enum reply = tsocket.nb_transport_bw(*b_outgoing, phase);
    if (reply == TLM_UPDATED) {
      SC_LOG_ASSERT(this, phase == ARM::AXI::B_READY,
                    "AXI TLM Protocol: Unexpected phase");
      b_outgoing->unref();
      b_outgoing = nullptr;
      b_beat_ready = false;
    }
  }

  stats.end_cycle(this->name());
}

// -------------------------------------------------------
// Transport Functions
// -------------------------------------------------------
tlm_sync_enum Cache::nb_transport_fw(ARM::AXI::Payload &payload,
                                     ARM::AXI::Phase &phase) {
  // Skip cache if AXI signal not set
  if (payload.cache != ARM::AXI::CACHE_AR_WRITE_THROUGH_RWA)
    return isocket.nb_transport_fw(payload, phase);

  switch (phase) {
  case ARM::AXI::AR_VALID:
    ar_queue_in.push_back(&payload);
    payload.ref();
    return TLM_ACCEPTED;
  case ARM::AXI::R_READY:
    return TLM_ACCEPTED;
  case ARM::AXI::AW_VALID:
    aw_queue_in.push_back(&payload);
    payload.ref();
    return TLM_ACCEPTED;
  case ARM::AXI::W_VALID:
    w_queue_in.push_back(&payload);
    return TLM_ACCEPTED;
  case ARM::AXI::W_VALID_LAST:
    w_queue_in.push_back(&payload);
    payload.ref();
    return TLM_ACCEPTED;
  case ARM::AXI::B_READY:
    return TLM_ACCEPTED;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unrecognized phase");
    return TLM_ACCEPTED;
  }
}

tlm_sync_enum Cache::nb_transport_bw(ARM::AXI::Payload &payload,
                                     ARM::AXI::Phase &phase) {
  // Skip cache if AXI signal not set
  if (payload.cache != ARM::AXI::CACHE_AR_WRITE_THROUGH_RWA)
    return tsocket.nb_transport_bw(payload, phase);

  // Handle cache line requests and store buffer writes
  switch (phase) {
  case ARM::AXI::AR_READY:
    ar_state = ACK;
    return TLM_ACCEPTED;
  case ARM::AXI::R_VALID:
    phase = ARM::AXI::R_READY;
    return TLM_UPDATED;
  case ARM::AXI::R_VALID_LAST:
    complete_cacheline_read(&payload);
    phase = ARM::AXI::R_READY;
    return TLM_UPDATED;
  case ARM::AXI::AW_READY:
    aw_state = ACK;
    return TLM_ACCEPTED;
  case ARM::AXI::W_READY:
    w_state = ACK;
    return TLM_ACCEPTED;
  case ARM::AXI::B_VALID:
    complete_storebuffer_write(&payload);
    phase = ARM::AXI::B_READY;
    return TLM_UPDATED;
  default:
    SC_LOG_ERROR(this, "AXI TLM Protocol: Unrecognized phase");
    return TLM_ACCEPTED;
  }
}

// -------------------------------------------------------
// Helper Functions
// -------------------------------------------------------
uint32_t Cache::set_beat_address(uint32_t base, unsigned beat_idx,
                                 unsigned beat_bytes, unsigned beats,
                                 ARM::AXI::Burst burst) {
  switch (burst) {
  case ARM::AXI::BURST_FIXED:
    return base;
  case ARM::AXI::BURST_INCR:
    return base + static_cast<uint32_t>(beat_idx * beat_bytes);
  case ARM::AXI::BURST_WRAP: {
    const uint32_t burst_size =
        static_cast<uint32_t>(beat_bytes * beats); // Wrap boundary
    const uint32_t aligned_base =
        (base / burst_size) * burst_size; // Floor to boundary
    const uint32_t incr_address =
        base + static_cast<uint64_t>(beat_idx * beat_bytes);
    return aligned_base | (incr_address & (burst_size - 1));
  }
  default:
    // Treat unknown as INCR (safe fallback)
    return base + static_cast<uint32_t>(beat_idx * beat_bytes);
  }
}

void Cache::enqueue_cacheline_read() {
  CacheLineRequest &clr = cache_line_requests.front();

  unsigned axi_bytes = axi_width / 8;
  unsigned beats = (cache_block_size + axi_bytes - 1) / axi_bytes;
  uint8_t len = beats - 1;
  ARM::AXI::Size size = get_axi_size(axi_width);

  ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
      ARM::AXI::COMMAND_READ, clr.address, size, len, ARM::AXI::BURST_INCR);

  UserSignals user;
  user.src_chiplet = chiplet_id;
  user.dst_chiplet = chiplet_id;
  user.src_module =
      ChipletRegistry::instance().get_module(chiplet_id, "memory")->id;
  user.fixed_address = true;

  payload->user = user.encode();
  payload->cache = ARM::AXI::CACHE_AW_WRITE_THROUGH_RWA;

  ar_queue_out.push_back(payload);

  active_cache_line_requests[payload] = clr;

  cache_line_requests.pop_front();
}

void Cache::complete_cacheline_read(ARM::AXI::Payload *payload) {
  auto it = active_cache_line_requests.find(payload);
  if (it != active_cache_line_requests.end()) {
    // Fill in cache line
    CacheLineRequest clr = it->second;
    clr.line->valid = true;
    clr.line->tag = clr.tag;
    payload->read_out(clr.line->data.data());

    active_cache_line_requests.erase(it);
  }
}

void Cache::enqueue_storebuffer_write() {
  StoreBufferEntry &sbe = store_buffer[store_buffer_head];

  if (sbe.ongoing)
    return;

  sbe.ongoing = true;

  unsigned axi_bytes = axi_width / 8;
  unsigned beats = (sbe.data.size() + axi_bytes - 1) / axi_bytes;
  uint8_t len = beats - 1;
  ARM::AXI::Size size = get_axi_size(axi_width);

  ARM::AXI::Payload *payload = ARM::AXI::Payload::new_payload(
      ARM::AXI::COMMAND_WRITE, sbe.address, size, len, ARM::AXI::BURST_INCR);

  payload->write_in(sbe.data.data());

  UserSignals user;
  user.src_chiplet = chiplet_id;
  user.dst_chiplet = chiplet_id;
  user.src_module =
      ChipletRegistry::instance().get_module(chiplet_id, "memory")->id;
  user.fixed_address = true;

  payload->user = user.encode();
  payload->cache = ARM::AXI::CACHE_AW_WRITE_THROUGH_RWA;

  aw_queue_out.push_back(payload);
}

void Cache::complete_storebuffer_write(ARM::AXI::Payload *payload) {
  StoreBufferEntry &sbe = store_buffer[store_buffer_head];
  sbe.ongoing = false;

  // Move head forward
  store_buffer_head = (store_buffer_head + 1) % store_buffer.size();
}

// -------------------------------------------------------
// Debug Functions
// -------------------------------------------------------
void Cache::dump() {
  std::cout << "=== Cache Dump ===\n";
  for (size_t i = 0; i < cache_lines.size(); ++i) {
    const auto &line = cache_lines[i];
    std::cout << "Line " << i << " [valid=" << line.valid
              << ", tag=" << std::setw(2) << line.tag << "]: ";
    for (uint8_t b : line.data) {
      std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b
                << " ";
    }
    std::cout << std::dec << "\n";
  }
}