#include "modules/Cache.h"

#include "common/protocol/ChipletExtension.h"
#include "common/protocol/ChipletPayload.h"

Cache::Cache(sc_module_name name, unsigned int chip_id, unsigned int cache_size,
             unsigned int cache_block_size,
             unsigned int cache_store_buffer_size,
             sc_time cache_arbitration_delay, sc_time cache_access_delay)
    : sc_module(name), chip_id(chip_id), cache_size(cache_size),
      cache_block_size(cache_block_size),
      cache_store_buffer_size(cache_store_buffer_size),
      cache_arbitration_delay(cache_arbitration_delay),
      cache_access_delay(cache_access_delay), utilization_tracker(this->name()),
      peq("peq") {
  if (cache_size % cache_block_size != 0)
    SC_REPORT_ERROR("Cache", "Cache size must be multiple of block size.");

  num_lines = cache_size / cache_block_size;
  cache_lines.resize(num_lines);

  for (auto &line : cache_lines) {
    line.valid = false;
    line.tag = 0;
    line.data.resize(cache_block_size, 0);
  }

  num_accesses = 0;
  num_hits = 0;
  num_misses = 0;

  tsocket.register_nb_transport_fw(this, &Cache::nb_transport_fw);
  isocket.register_nb_transport_bw(this, &Cache::nb_transport_bw);

  SC_THREAD(process_request_queue);
  SC_THREAD(process_transaction);
  sensitive << peq.get_event();

  SC_THREAD(drain_store_buffer);
}

void Cache::process_transaction() {
  while (true) {
    wait();

    tlm_generic_payload *transaction = peq.get_next_transaction();

    utilization_tracker.set_active();
    access_cache(*transaction);
    utilization_tracker.set_idle();

    if (transaction->is_read()) {
      tlm_phase phase = BEGIN_RESP;
      sc_time delay = SC_ZERO_TIME;
      tsocket->nb_transport_bw(*transaction, phase, delay);
      resp_evt.notify(delay);
    }
  }
}

void Cache::process_request_queue() {
  while (true) {
    wait(req_evt);

    while (!request_queue.empty()) {
      Request request = request_queue.front();
      request_queue.pop_front();

      tlm_generic_payload *transaction = request.transaction;
      ChipletExtension *ext = transaction->get_extension<ChipletExtension>();
      tlm_phase phase = UNINITIALIZED_PHASE;
      sc_time delay = *request.delay;

      bool skip_cache = !ext->fixed_address || ext->is_volatile ||
                        ext->destination_id != chip_id;

      if (skip_cache) {
        SC_LOG_DEBUG(this, *transaction, "Cache skipped");

        cache_skipped[transaction] = true;

        phase = BEGIN_REQ;
        isocket->nb_transport_fw(*transaction, phase, delay);
      } else {
        peq.notify(*transaction, delay);

        phase = END_REQ;
        tsocket->nb_transport_bw(*transaction, phase, delay);

        wait(resp_evt);
      }
    }
  }
}

void Cache::access_cache(tlm_generic_payload &transaction) {
  ChipletExtension *ext = transaction.get_extension<ChipletExtension>();

  uint32_t start_addr = transaction.get_address();
  unsigned char *data_ptr = transaction.get_data_ptr();
  unsigned int data_length = transaction.get_data_length();

  unsigned int num_beats = ext->axi_length + 1; // AxLEN + 1
  unsigned int beat_bytes = 1 << ext->axi_size; // 2^AxSIZE

  std::unordered_map<uint32_t, bool> updated_cache_indices;

  for (unsigned int beat = 0; beat < num_beats; ++beat) {
    uint32_t beat_addr = start_addr;

    switch (ext->axi_burst) {
    // FIXED
    case 0:
      break;
    // INCR
    case 1:
      beat_addr = start_addr + beat * beat_bytes;
      break;
    // WRAP
    case 2: {
      unsigned int burst_size_bytes = num_beats * beat_bytes;
      uint32_t base = (start_addr / burst_size_bytes) * burst_size_bytes;
      uint32_t offset = (start_addr + beat * beat_bytes) % burst_size_bytes;
      beat_addr = base + offset;
      break;
    }
    default:
      transaction.set_response_status(TLM_BURST_ERROR_RESPONSE);
      SC_LOG_ERROR(this, transaction, "Unsupported AXI burst type");
      return;
    }

    // copy possibly across cache lines
    unsigned int remaining = beat_bytes;
    unsigned int beat_offset_in_tx = beat * beat_bytes;

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

      if (transaction.is_read()) {
        if (!(line.valid && line.tag == tag)) {
          // Read Miss: fetch whole line
          SC_LOG_DEBUG(this, transaction, "Read Miss: Loading data from RAM");
          num_misses++;

          uint8_t *buffer = new uint8_t[cache_block_size];
          send_axi_request(transaction, TLM_READ_COMMAND, block_address, buffer,
                           cache_block_size);

          // update same line only once
          if (!updated_cache_indices[index]) {
            line.valid = true;
            line.tag = tag;
            std::memcpy(line.data.data(), buffer, cache_block_size);
            updated_cache_indices[index] = true;
          }

          // copy to transaction buffer
          std::memcpy(data_ptr + beat_offset_in_tx + (beat_bytes - remaining),
                      buffer + block_offset, copy_len);

          delete[] buffer;
        } else {
          // Read Hit
          SC_LOG_DEBUG(this, transaction, "Read Hit: Loading data from cache");
          num_hits++;

          // copy to transaction buffer
          std::memcpy(data_ptr + beat_offset_in_tx + (beat_bytes - remaining),
                      &line.data[block_offset], copy_len);

          wait(delays.cache_access(transaction));
        }
      } else if (transaction.is_write()) {
        if (line.valid && line.tag == tag) {
          // Write Hit
          SC_LOG_DEBUG(this, transaction,
                       "Write Hit: Writing data to cache and RAM");
          num_hits++;

          // copy to line
          std::memcpy(&line.data[block_offset],
                      data_ptr + beat_offset_in_tx + (beat_bytes - remaining),
                      copy_len);

          wait(delays.cache_access(transaction));

          updated_cache_indices[index] = true;
        } else {
          SC_LOG_DEBUG(this, transaction,
                       "Write Miss: Writing data only to RAM");
          num_misses++;
        }

        // enqueue into store buffer
        while (store_buffer.size() >= cache_store_buffer_size) {
          wait(store_buffer_out_evt); // stall until space frees up
        }

        StoreBufferEntry entry;
        entry.transaction = &transaction;
        entry.wlast = (beat == num_beats - 1) && (remaining - copy_len == 0);
        entry.address = block_address + block_offset;
        entry.data.resize(copy_len);
        std::memcpy(entry.data.data(),
                    data_ptr + beat_offset_in_tx + (beat_bytes - remaining),
                    copy_len);
        store_buffer.push_back(std::move(entry));
        store_buffer_in_evt.notify(SC_ZERO_TIME);
      }

      // advance inside this beat
      beat_addr += copy_len;
      remaining -= copy_len;
    }
  }
}

void Cache::drain_store_buffer() {
  while (true) {
    wait(store_buffer_in_evt);

    while (!store_buffer.empty()) {
      StoreBufferEntry entry = std::move(store_buffer.front());
      store_buffer.pop_front();
      store_buffer_out_evt.notify(SC_ZERO_TIME);

      send_axi_request(*entry.transaction, TLM_WRITE_COMMAND, entry.address,
                       entry.data.data(), entry.data.size());

      if (entry.wlast) {
        tlm_phase phase = BEGIN_RESP;
        sc_time delay = SC_ZERO_TIME;
        tsocket->nb_transport_bw(*entry.transaction, phase, delay);
        resp_evt.notify(delay);
      }
    }
  }
}

void Cache::send_axi_request(tlm_generic_payload &transaction,
                             tlm_command command, uint32_t address,
                             unsigned char *data, unsigned int data_length) {
  ChipletPayload *axi_trans =
      static_cast<ChipletPayload &>(transaction).clone_ext();

  axi_trans->set_command(command);
  axi_trans->set_address(address);
  axi_trans->set_data_ptr(data, false);
  axi_trans->set_data_length(data_length);

  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;

  isocket->nb_transport_fw(*axi_trans, phase, delay);

  wait(axi_resp_evt);

  delete axi_trans;
}

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

void Cache::report_rates() {
  std::cout << "  Cache Accesses: " << num_accesses << std::endl;
  if (num_accesses > 0) {
    double hit_rate = double(100) * num_hits / num_accesses;
    double miss_rate = double(100) * num_misses / num_accesses;
    std::cout << "  Hit Rate: " << std::dec << hit_rate << "%\n";
    std::cout << "  Miss Rate: " << std::dec << miss_rate << "%\n";
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum Cache::nb_transport_fw(tlm_generic_payload &transaction,
                                     tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase);

  switch (phase) {
  case BEGIN_REQ:
    delay += delays.cache_arbitration(transaction);

    if (transaction.is_write()) {
      request_queue.push_back({&transaction, &phase, &delay});
    } else {
      request_queue.push_front({&transaction, &phase, &delay});
    }

    req_evt.notify(SC_ZERO_TIME);

    return TLM_ACCEPTED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum Cache::nb_transport_bw(tlm_generic_payload &transaction,
                                     tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase);

  auto it = cache_skipped.find(&transaction);
  if (it != cache_skipped.end()) {
    switch (phase) {
    case END_REQ:
      delay += delays.cache_arbitration(transaction);
      break;
    case BEGIN_RESP:
      cache_skipped.erase(it);
      break;
    }

    return tsocket->nb_transport_bw(transaction, phase, delay);
  }

  switch (phase) {
  case BEGIN_RESP:
    axi_resp_evt.notify(delay);
    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}