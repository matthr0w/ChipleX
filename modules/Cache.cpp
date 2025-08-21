#include "Cache.h"

#include "common/protocol/ChipletExtension.h"
#include "common/protocol/ChipletPayload.h"

Cache::Cache(sc_module_name name, AXIUtils &axi_utils, unsigned int chip_id,
             unsigned int cache_size, unsigned int cache_block_size,
             unsigned int cache_store_buffer_size,
             sc_time cache_arbitration_delay, sc_time cache_access_delay)
    : sc_module(name), axi_utils(axi_utils), chip_id(chip_id),
      cache_size(cache_size), cache_block_size(cache_block_size),
      cache_store_buffer_size(cache_store_buffer_size),
      cache_arbitration_delay(cache_arbitration_delay),
      cache_access_delay(cache_access_delay), utilization_tracker(this->name()),
      peq("peq") {
  if (cache_size % cache_block_size != 0)
    SC_REPORT_ERROR("Cache", "Cache size must be multiple of block size.");

  num_lines = cache_size / cache_block_size;
  cache_lines.resize(num_lines);

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
  tlm_generic_payload *transaction = nullptr;
  tlm_phase phase = UNINITIALIZED_PHASE;
  sc_time delay = SC_ZERO_TIME;

  while (true) {
    wait();

    transaction = peq.get_next_transaction();

    utilization_tracker.set_active();
    access_cache(*transaction);
    utilization_tracker.set_idle();

    delay = SC_ZERO_TIME;
    phase = END_REQ;
    tsocket->nb_transport_bw(*transaction, phase, delay);

    if (transaction->is_read()) {
      delay = SC_ZERO_TIME;
      phase = BEGIN_RESP;
      tsocket->nb_transport_bw(*transaction, phase, delay);
      resp_evt.notify(delay);
    }
  }
}

void Cache::process_request_queue() {
  tlm_generic_payload *transaction = nullptr;
  ChipletExtension *ext = nullptr;
  tlm_phase phase = UNINITIALIZED_PHASE;
  sc_time delay = SC_ZERO_TIME;

  while (true) {
    wait(req_evt);

    while (!request_queue.empty()) {
      Request request = request_queue.front();
      request_queue.pop_front();

      transaction = request.transaction;
      delay = *request.delay;

      transaction->get_extension(ext);

      bool skip_cache = !ext->fixed_address || ext->is_volatile ||
                        ext->destination_id != chip_id;

      if (skip_cache) {
        SC_LOG_DEBUG(this, *transaction, "Cache skipped");

        cache_skipped[transaction] = true;

        phase = BEGIN_REQ;
        isocket->nb_transport_fw(*transaction, phase, delay);
      } else {
        peq.notify(*transaction, delay);
        wait(resp_evt);
      }
    }
  }
}

void Cache::access_cache(tlm_generic_payload &transaction) {
  uint32_t address = transaction.get_address();
  unsigned char *data_ptr = transaction.get_data_ptr();
  unsigned int data_length = transaction.get_data_length();

  // track cache lines that were filled from RAM in this transaction
  std::unordered_map<uint32_t, bool> updated_cache_indices;

  // process each block covered by the request
  unsigned int processed = 0;
  while (processed < data_length) {
    num_accesses++;

    // mask tag + index bits
    uint32_t block_address = (address + processed) & ~(cache_block_size - 1);
    // mask offset bits
    uint32_t block_offset = (address + processed) & (cache_block_size - 1);

    // extract tag + index bits
    // shift offset and index bits
    uint32_t tag =
        block_address >> (static_cast<uint32_t>(log2(cache_block_size)) +
                          static_cast<uint32_t>(log2(num_lines)));
    // shift offset bits and mask index bits
    uint32_t index =
        (block_address >> static_cast<uint32_t>(log2(cache_block_size))) %
        num_lines;

    // data size to fit in cache line
    uint32_t length =
        std::min(cache_block_size - block_offset, data_length - processed);

    CacheLine &line = cache_lines[index];

    // resize cache line if needed
    if (line.data.size() != cache_block_size) {
      line.data.resize(cache_block_size, 0);
    }

    if (transaction.is_read()) {
      // cache miss if invalid or wrong tag
      if (!(line.valid && line.tag == tag)) {
        SC_LOG_DEBUG(this, transaction, "Read Miss: Loading data from RAM");

        num_misses++;

        uint8_t *buffer = new uint8_t[cache_block_size];

        send_axi_request(transaction, TLM_READ_COMMAND, block_address, buffer,
                         cache_block_size);

        // only update cache line if index has NOT been filled already in this
        // transaction
        if (!updated_cache_indices[index]) {
          // fill cache line
          line.valid = true;
          line.tag = tag;
          std::memcpy(line.data.data(), buffer, cache_block_size);

          // mark as updated
          updated_cache_indices[index] = true;
        } else {
          SC_LOG_DEBUG(this, transaction,
                       "Cache line already loaded in this transaction -> "
                       "skipping cache update");
        }

        // copy required block portion directly to transaction buffer
        std::memcpy(data_ptr + processed, buffer + block_offset, length);

        delete[] buffer;
      } else {
        // cache hit: use data from cache
        SC_LOG_DEBUG(this, transaction, "Read Hit: Loading data from cache");

        num_hits++;

        std::memcpy(data_ptr + processed, &line.data[block_offset], length);
        wait(delays.cache_access(transaction));
      }
    } else if (transaction.is_write()) {
      // cache hit if valid and correct tag
      if (line.valid && line.tag == tag) {
        SC_LOG_DEBUG(this, transaction,
                     "Write Hit: Writing data to cache and RAM");

        num_hits++;

        std::memcpy(&line.data[block_offset], data_ptr + processed, length);
        wait(delays.cache_access(transaction));

        // mark as updated
        updated_cache_indices[index] = true;
      } else {
        SC_LOG_DEBUG(this, transaction, "Write Miss: Writing data only to RAM");

        num_misses++;
      }

      // enqueue into store buffer
      while (store_buffer.size() >= cache_store_buffer_size) {
        wait(store_buffer_out_evt); // stall until space frees up
      }

      StoreBufferEntry entry;
      entry.transaction = &transaction;
      entry.wlast = processed + length >= data_length;
      entry.address = block_address + block_offset;
      entry.data.resize(length);
      std::memcpy(entry.data.data(), data_ptr + processed, length);
      store_buffer.push_back(std::move(entry));
      store_buffer_in_evt.notify(SC_ZERO_TIME);
    }

    processed += length;
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
  auto *axi_trans = static_cast<ChipletPayload &>(transaction).clone_ext();

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