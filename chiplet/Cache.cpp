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

    utilization_tracker.set_active();

    transaction = peq.get_next_transaction();
    transaction->get_extension(ext);

    uint32_t address = transaction->get_address();
    unsigned char *data = transaction->get_data_ptr();
    unsigned int data_size = transaction->get_data_length();

    // skip cache for dynamic or off-chip transactions
    if (!ext->fixed_address || ext->destination_id != chiplet_id) {
      SC_LOG_DEBUG(this, *transaction,
                   "Dynamic address or off-chip request -> skipping cache");
      send_to_bus(*transaction);
    }
    // TODO: fix this
    // skip cache for larger than cache transactions
    else if (data_size > cache_size) {
      SC_LOG_DEBUG(this, *transaction,
                   "Too large data request -> skipping cache");
      send_to_bus(*transaction);
    } else {
      split_transaction(*transaction);
    }

    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    tlm_resp = core_target_socket->nb_transport_bw(*transaction, phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
    }

    utilization_tracker.set_idle();
  }
}

void chiplet::Cache::split_transaction(tlm_generic_payload &transaction) {
  uint32_t address = transaction.get_address();
  unsigned char *data_ptr = transaction.get_data_ptr();
  unsigned int data_size = transaction.get_data_length();

  // process each block covered by the request
  unsigned int processed = 0;
  while (processed < data_size) {
    uint32_t tag, index;
    // mask tag + index bits
    uint32_t block_address = (address + processed) & ~(block_size - 1);
    // mask offset bits
    uint32_t block_offset = (address + processed) & (block_size - 1);
    uint32_t length =
        std::min(block_size - block_offset, data_size - processed);

    // extract tag + index bits
    parse_address(block_address, tag, index);

    CacheLine &line = cache_lines[index];

    // resize line data if needed
    if (line.data.size() != block_size) {
      line.data.resize(block_size, 0);
    }

    if (transaction.is_read()) {
      // load whole block from RAM if invalid or wrong tag (cache miss)
      if (!(line.valid && line.tag == tag)) {
        SC_LOG_DEBUG(this, transaction,
                     "CACHE MISS: Loading data from RAM to cache");

        auto *tmp_trans =
            static_cast<ChipletPayload &>(transaction).clone_ext();

        unsigned char *tmp_data = new unsigned char[block_size]();

        tmp_trans->set_command(TLM_READ_COMMAND);
        tmp_trans->set_address(block_address);
        tmp_trans->set_data_ptr(tmp_data);
        tmp_trans->set_data_length(block_size);

        send_to_bus(*tmp_trans);

        // fill cache line
        line.valid = true;
        line.tag = tag;
        std::memcpy(line.data.data(), tmp_data, block_size);

        delete tmp_trans;
      }

      // copy data from cache to transaction buffer
      std::memcpy(data_ptr + processed, &line.data[block_offset], length);
      wait(get_cache_access_delay(*this, transaction, access_delay));
    } else if (transaction.is_write()) {
      // write data to cache if valid and correct tag (cache hit)
      if (line.valid && line.tag == tag) {
        SC_LOG_DEBUG(this, transaction, "CACHE HIT: Writing data to cache");
        std::memcpy(&line.data[block_offset], data_ptr + processed, length);
        wait(get_cache_access_delay(*this, transaction, access_delay));
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

void chiplet::Cache::parse_address(uint32_t address, uint32_t &tag,
                                   uint32_t &index) {
  // shift offset bits and mask lower index bits
  index = (address >> static_cast<uint32_t>(log2(block_size))) % num_lines;
  // shift offset and index bits
  tag = address >> (static_cast<uint32_t>(log2(block_size)) +
                    static_cast<uint32_t>(log2(num_lines)));
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