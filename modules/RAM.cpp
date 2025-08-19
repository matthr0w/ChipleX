#include "RAM.h"

#include "common/Delays.h"
#include "common/Tracker.h"
#include "common/protocol/ChipletExtension.h"

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

RAM::RAM(sc_module_name name, unsigned int ram_size, unsigned int ram_width,
         sc_time ram_clk_cycle, sc_time ram_address_delay,
         sc_time ram_access_delay)
    : sc_module(name), ram_size(ram_size), ram_width(ram_width),
      ram_clk_cycle(ram_clk_cycle), ram_address_delay(ram_address_delay),
      ram_access_delay(ram_access_delay), utilization_tracker(this->name()),
      peq("peq"), mem(ram_size * 1024, 0), write_flags(mem.size(), false) {
  target_socket.register_nb_transport_fw(this, &RAM::nb_transport_fw);

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

    uint32_t address = transaction->get_address();
    unsigned char *data_ptr = transaction->get_data_ptr();
    unsigned int data_length = transaction->get_data_length();

    unsigned int num_transfers = ext->axi_length;
    unsigned int beat_bytes = ext->axi_size;

    for (unsigned int beat = 0; beat < num_transfers; ++beat) {
      uint32_t beat_addr = address;

      switch (ext->axi_burst) {
      // FIXED
      case 0:
        break;
      // INCR
      case 1:
        beat_addr = address + beat * beat_bytes;
        break;
      // WRAP
      case 2: {
        unsigned int burst_size_bytes = num_transfers * beat_bytes;
        uint32_t base = (address / burst_size_bytes) * burst_size_bytes;
        uint32_t offset = (address + beat * beat_bytes) % burst_size_bytes;
        beat_addr = base + offset;
        break;
      }
      default:
        transaction->set_response_status(TLM_BURST_ERROR_RESPONSE);
        SC_LOG_ERROR(this, *transaction, "Unsupported AXI burst type");
        return;
      }

      if (beat_addr + beat_bytes > mem.size()) {
        transaction->set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
        SC_LOG_ERROR(this, *transaction, "Out of bounds RAM access");
        return;
      }

      if (transaction->get_command() == TLM_READ_COMMAND) {
        std::memcpy(&data_ptr[beat * beat_bytes], &mem[beat_addr], beat_bytes);
      } else if (transaction->get_command() == TLM_WRITE_COMMAND) {
        std::memcpy(&mem[beat_addr], &data_ptr[beat * beat_bytes], beat_bytes);

        // set written flags
        for (unsigned int i = 0; i < beat_bytes; ++i) {
          write_flags[beat_addr + i] = true;
        }
      }
    }

    // off-chip requests: request done on last flit
    // if (transaction->get_command() == TLM_WRITE_COMMAND) {
    //   transaction->get_extension(ext);
    //   if (ext->flit_id == ext->flit_count - 1) {
    //     sc_time latency = sc_time_stamp() - ext->start_time;
    //     LatencyTracker::instance().record(latency);
    //   }
    // }

    utilization_tracker.set_idle();

    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    target_socket->nb_transport_bw(*transaction, phase, delay);

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

      target_socket->nb_transport_bw(*transaction, phase, delay);

      peq.notify(*transaction, delay);

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
    delay += get_mem_access_delay(*this, transaction, ram_clk_cycle,
                                  ram_access_delay, ram_width);

    requests_queue.push_back({&transaction, &phase, &delay});
    req_evt.notify(SC_ZERO_TIME);

    return TLM_ACCEPTED;
  }

  return TLM_ACCEPTED;
}