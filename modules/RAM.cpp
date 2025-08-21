#include "RAM.h"

#include "common/protocol/ChipletExtension.h"

RAM::RAM(sc_module_name name, unsigned int ram_size, unsigned int ram_width,
         sc_time ram_clk_cycle, sc_time ram_access_delay)
    : sc_module(name), ram_size(ram_size), ram_width(ram_width),
      ram_clk_cycle(ram_clk_cycle), ram_access_delay(ram_access_delay),
      utilization_tracker(this->name()), peq("peq"), mem(ram_size * 1024, 0),
      write_flags(mem.size(), false) {
  tsocket.register_nb_transport_fw(this, &RAM::nb_transport_fw);

  SC_THREAD(process_queue);

  SC_THREAD(process_transaction);
  sensitive << peq.get_event();
}

void RAM::process_transaction() {
  tlm_generic_payload *transaction;
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;

  while (true) {
    wait();

    utilization_tracker.set_active();

    transaction = peq.get_next_transaction();
    transaction->get_extension(ext);

    uint32_t start_addr = transaction->get_address();
    unsigned char *data_ptr = transaction->get_data_ptr();
    unsigned int data_length = transaction->get_data_length();

    unsigned int num_beats = ext->axi_length + 1; // AxLEN + 1
    unsigned int beat_bytes = 1 << ext->axi_size; // 2^AxSIZE

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
        transaction->set_response_status(TLM_BURST_ERROR_RESPONSE);
        SC_LOG_ERROR(this, *transaction, "Unsupported AXI burst type");
        return;
      }

      // out of bounds check
      if (beat_addr >= mem.size()) {
        transaction->set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
        SC_LOG_ERROR(this, *transaction, "Out of bounds RAM access");
        return;
      }

      // data buffer check
      unsigned int bytes_left =
          data_length > beat * beat_bytes ? data_length - beat * beat_bytes : 0;
      unsigned int bytes_to_copy = std::min(beat_bytes, bytes_left);

      if (bytes_to_copy == 0)
        break;

      if (transaction->get_command() == TLM_READ_COMMAND) {
        std::memcpy(&data_ptr[beat * beat_bytes], &mem[beat_addr],
                    bytes_to_copy);
      } else if (transaction->get_command() == TLM_WRITE_COMMAND) {
        std::memcpy(&mem[beat_addr], &data_ptr[beat * beat_bytes],
                    bytes_to_copy);

        // set written flags
        for (unsigned int i = 0; i < bytes_to_copy; ++i) {
          write_flags[beat_addr + i] = true;
        }
      }
    }

    utilization_tracker.set_idle();

    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    tsocket->nb_transport_bw(*transaction, phase, delay);

    resp_evt.notify(delay);
  }
}

void RAM::process_queue() {
  while (true) {
    wait(req_evt);

    while (!requests_queue.empty()) {
      Request request = requests_queue.front();
      requests_queue.pop_front();

      tlm_generic_payload *transaction = request.transaction;
      tlm_phase phase = END_REQ;
      sc_time delay = *request.delay;

      peq.notify(*transaction, delay);

      tsocket->nb_transport_bw(*transaction, phase, delay);

      wait(resp_evt);
    }
  }
}

void RAM::report_usage() {
  size_t written_bytes =
      std::count(write_flags.begin(), write_flags.end(), true);

  std::cout << "  Used Bytes: " << written_bytes << " / " << mem.size() << " ("
            << (100.0 * written_bytes / mem.size()) << "%)\n";
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum RAM::nb_transport_fw(tlm_generic_payload &transaction,
                                   tlm_phase &phase, sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase);

  switch (phase) {
  case BEGIN_REQ:
    delay += delays.ram_access(transaction);

    requests_queue.push_back({&transaction, &phase, &delay});
    req_evt.notify(SC_ZERO_TIME);

    return TLM_ACCEPTED;
  }

  return TLM_ACCEPTED;
}