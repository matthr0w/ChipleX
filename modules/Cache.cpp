#include "Cache.h"

#include "common/Delays.h"
#include "common/Tracker.h"
#include "common/protocol/ChipletExtension.h"

#include "include/logging.h"

Cache::Cache(sc_module_name name, unsigned int chip_id, unsigned int cache_size,
             unsigned int cache_block_size, sc_time cache_arbitration_delay,
             sc_time cache_access_delay, unsigned int bus_width,
             sc_time bus_clk_cycle)
    : sc_module(name), chip_id(chip_id), cache_size(cache_size),
      cache_block_size(cache_block_size),
      cache_arbitration_delay(cache_arbitration_delay),
      cache_access_delay(cache_access_delay), bus_width(bus_width),
      bus_clk_cycle(bus_clk_cycle), utilization_tracker(this->name()),
      peq("peq") {
  if (cache_size % cache_block_size != 0) {
    SC_REPORT_ERROR("Cache", "Cache size must be multiple of block size.");
  }

  num_lines = cache_size / cache_block_size;
  cache_lines.resize(num_lines);

  num_accesses = 0;
  num_hits = 0;
  num_misses = 0;

  target_socket.register_nb_transport_fw(this, &Cache::nb_transport_fw);
  initiator_socket.register_nb_transport_bw(this, &Cache::nb_transport_bw);

  SC_THREAD(process_queue);

  SC_THREAD(process_transaction);
  sensitive << peq.get_event();
}

void Cache::process_transaction() {
  tlm_generic_payload *transaction;
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;

  while (true) {
    wait();

    transaction = peq.get_next_transaction();
    transaction->get_extension(ext);

    bool skip_cache = !ext->fixed_address || ext->is_volatile ||
                      ext->destination_id != chip_id;

    if (skip_cache) {
      SC_LOG_DEBUG(
          this, *transaction,
          "Non-fixed address, volatile or off-chip request -> skipping cache");
      transport_fw(*transaction);
    } else {
      utilization_tracker.set_active();
      access_cache(*transaction);
      utilization_tracker.set_idle();
    }

    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    target_socket->nb_transport_bw(*transaction, phase, delay);

    resp_evt.notify(delay);
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

        auto *tmp_trans =
            static_cast<ChipletPayload &>(transaction).clone_ext();

        unsigned char *tmp_data = new unsigned char[cache_block_size]();

        tmp_trans->set_command(TLM_READ_COMMAND);
        tmp_trans->set_address(block_address);
        tmp_trans->set_data_ptr(tmp_data);
        tmp_trans->set_data_length(cache_block_size);

        transport_fw(*tmp_trans);

        // only update cache line if index has NOT been filled already in this
        // transaction
        if (!updated_cache_indices[index]) {
          // fill cache line
          line.valid = true;
          line.tag = tag;
          std::memcpy(line.data.data(), tmp_data, cache_block_size);

          // mark as updated
          updated_cache_indices[index] = true;
        } else {
          SC_LOG_DEBUG(
              this, transaction,
              "SKIPPING CACHE LINE UPDATE: Already loaded in this transaction");
        }

        // copy required block portion directly to transaction buffer
        std::memcpy(data_ptr + processed, tmp_data + block_offset, length);

        delete tmp_trans;
      } else {
        // cache hit: use data from cache
        SC_LOG_DEBUG(this, transaction, "Read Hit: Loading data from cache");

        num_hits++;

        std::memcpy(data_ptr + processed, &line.data[block_offset], length);
        wait(get_cache_access_delay(*this, transaction, cache_access_delay));
      }
    } else if (transaction.is_write()) {
      // cache hit if valid and correct tag
      if (line.valid && line.tag == tag) {
        SC_LOG_DEBUG(this, transaction,
                     "Write Hit: Writing data to cache and RAM");

        num_hits++;

        std::memcpy(&line.data[block_offset], data_ptr + processed, length);
        wait(get_cache_access_delay(*this, transaction, cache_access_delay));

        // mark as updated
        updated_cache_indices[index] = true;
      } else {
        SC_LOG_DEBUG(this, transaction, "CACHE MISS: Writing data only to RAM");

        num_misses++;
      }

      // write-through: write data to RAM
      auto *tmp_trans = static_cast<ChipletPayload &>(transaction).clone_ext();

      unsigned char *tmp_data = new unsigned char[length]();

      std::memcpy(tmp_data, data_ptr + processed, length);

      tmp_trans->set_command(TLM_WRITE_COMMAND);
      tmp_trans->set_address(block_address + block_offset);
      tmp_trans->set_data_ptr(tmp_data);
      tmp_trans->set_data_length(length);

      transport_fw(*tmp_trans);

      delete tmp_trans;
    }

    processed += length;
  }
}

void Cache::process_queue() {
  while (true) {
    wait(req_evt);

    while (!requests_queue.empty()) {
      Request request = requests_queue.front();
      requests_queue.pop_front();

      tlm_generic_payload *transaction = request.transaction;
      tlm_phase phase = END_REQ;
      sc_time delay = *request.delay;

      target_socket->nb_transport_bw(*transaction, phase, delay);

      peq.notify(*transaction, delay);

      wait(resp_evt);
    }
  }
}

void Cache::transport_fw(tlm_generic_payload &transaction) {
  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;

  initiator_socket->nb_transport_fw(transaction, phase, delay);

  wait(axi_resp_evt);
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
    delay += get_cache_arbitration_delay(*this, transaction,
                                         cache_arbitration_delay);

    requests_queue.push_back({&transaction, &phase, &delay});
    req_evt.notify(SC_ZERO_TIME);

    return TLM_ACCEPTED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum Cache::nb_transport_bw(tlm_generic_payload &transaction,
                                     tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase);

  switch (phase) {
  case BEGIN_RESP:
    ChipletExtension *ext;

    transaction.get_extension(ext);

    if (ext->source_id == -1) {
      delay += get_bus_transfer_bw_delay(*this, transaction, bus_clk_cycle,
                                         bus_width);
    } else {
      // request to interconnect
      // no direct data response -> no extra delay
      delay += SC_ZERO_TIME;
    }

    axi_resp_evt.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}