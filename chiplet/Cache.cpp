#include "Cache.h"

#include "common/Delays.h"
#include "common/Tracker.h"
#include "common/protocol/ChipletExtension.h"

#include "include/logging.h"

chiplet::Cache::Cache(sc_module_name name, unsigned int chiplet_id)
    : sc_module(name), utilization_tracker(this->name()),
      chiplet_id(chiplet_id), core_target_socket("core_target_socket"),
      bus_initiator_socket("bus_initiator_socket"), peq("peq") {
  if (cache_size % block_size != 0) {
    SC_REPORT_ERROR("Cache", "Cache size must be multiple of block size.");
  }

  num_lines = cache_size / block_size;
  cache_lines.resize(num_lines);

  num_accesses = 0;
  num_hits = 0;
  num_misses = 0;

  core_target_socket.register_nb_transport_fw(this, &Cache::nb_transport_fw);
  bus_initiator_socket.register_nb_transport_bw(this, &Cache::nb_transport_bw);

  SC_THREAD(process_transaction);
  sensitive << peq.get_event();
}

void chiplet::Cache::process_transaction() {
  tlm_generic_payload *transaction;
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq.get_next_transaction();
    transaction->get_extension(ext);

    uint32_t address = transaction->get_address();
    unsigned char *data = transaction->get_data_ptr();
    unsigned int data_size = transaction->get_data_length();

    // skip cache for dynamic, volatile or off-chip transactions
    if (!ext->fixed_address || ext->is_volatile ||
        ext->destination_id != chiplet_id) {
      SC_LOG_DEBUG(
          this, *transaction,
          "Dynamic address, volatile or off-chip request -> skipping cache");
      send_to_bus(*transaction);
    } else {
      utilization_tracker.set_active();
      split_transaction(*transaction);
      utilization_tracker.set_idle();
    }

    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    tlm_resp = core_target_socket->nb_transport_bw(*transaction, phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
    }
  }
}

void chiplet::Cache::split_transaction(tlm_generic_payload &transaction) {
  uint32_t address = transaction.get_address();
  unsigned char *data_ptr = transaction.get_data_ptr();
  unsigned int data_size = transaction.get_data_length();

  // track cache lines that were filled from RAM in this transaction
  std::unordered_map<uint32_t, bool> updated_cache_indices;

  // process each block covered by the request
  unsigned int processed = 0;
  while (processed < data_size) {
    num_accesses++;

    // mask tag + index bits
    uint32_t block_address = (address + processed) & ~(block_size - 1);
    // mask offset bits
    uint32_t block_offset = (address + processed) & (block_size - 1);

    // extract tag + index bits
    // shift offset and index bits
    uint32_t tag = block_address >> (static_cast<uint32_t>(log2(block_size)) +
                                     static_cast<uint32_t>(log2(num_lines)));
    // shift offset bits and mask index bits
    uint32_t index =
        (block_address >> static_cast<uint32_t>(log2(block_size))) % num_lines;

    // data size to fit in cache line
    uint32_t length =
        std::min(block_size - block_offset, data_size - processed);

    CacheLine &line = cache_lines[index];

    // resize cache line if needed
    if (line.data.size() != block_size) {
      line.data.resize(block_size, 0);
    }

    if (transaction.is_read()) {
      // cache miss if invalid or wrong tag
      if (!(line.valid && line.tag == tag)) {
        SC_LOG_DEBUG(this, transaction, "CACHE MISS: Loading data from RAM");

        num_misses++;

        auto *tmp_trans =
            static_cast<ChipletPayload &>(transaction).clone_ext();

        unsigned char *tmp_data = new unsigned char[block_size]();

        tmp_trans->set_command(TLM_READ_COMMAND);
        tmp_trans->set_address(block_address);
        tmp_trans->set_data_ptr(tmp_data);
        tmp_trans->set_data_length(block_size);

        send_to_bus(*tmp_trans);

        // only update cache line if index has NOT been filled already in this
        // transaction
        if (!updated_cache_indices[index]) {
          // fill cache line
          line.valid = true;
          line.tag = tag;
          std::memcpy(line.data.data(), tmp_data, block_size);

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
        SC_LOG_DEBUG(this, transaction, "CACHE HIT: Loading data from cache");

        num_hits++;

        std::memcpy(data_ptr + processed, &line.data[block_offset], length);
        wait(get_cache_access_delay(*this, transaction, access_delay));
      }
    } else if (transaction.is_write()) {
      // cache hit if valid and correct tag
      if (line.valid && line.tag == tag) {
        SC_LOG_DEBUG(this, transaction,
                     "CACHE HIT: Writing data to cache and RAM");

        num_hits++;

        std::memcpy(&line.data[block_offset], data_ptr + processed, length);
        wait(get_cache_access_delay(*this, transaction, access_delay));

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

      send_to_bus(*tmp_trans);

      delete tmp_trans;
    }

    processed += length;
  }
}

void chiplet::Cache::send_to_bus(tlm_generic_payload &transaction) {
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  phase = BEGIN_REQ;
  delay = SC_ZERO_TIME;

  tlm_resp = bus_initiator_socket->nb_transport_fw(transaction, phase, delay);

  if (tlm_resp == TLM_UPDATED) {
    wait(delay);
  }

  wait(bus_transaction_done);
}

void chiplet::Cache::report_rates() {
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
tlm_sync_enum chiplet::Cache::nb_transport_fw(tlm_generic_payload &transaction,
                                              tlm_phase &phase,
                                              sc_time &delay) {
  if (phase == BEGIN_REQ) {
    delay += get_cache_arbitration_delay(*this, transaction, arbitration_delay);

    peq.notify(transaction, delay);

    phase = END_REQ;
    return TLM_UPDATED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum chiplet::Cache::nb_transport_bw(tlm_generic_payload &transaction,
                                              tlm_phase &phase,
                                              sc_time &delay) {
  if (phase == BEGIN_RESP) {
    SC_LOG_DEBUG(this, transaction, "PROTOCOL: Received response from Bus");

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

    bus_transaction_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}