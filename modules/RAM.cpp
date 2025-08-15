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
  socket.register_nb_transport_fw(this, &RAM::nb_transport_fw);

  SC_THREAD(process_queue);

  SC_THREAD(process_transaction);
  sensitive << peq.get_event();
}

void RAM::process_transaction() {
  tlm_generic_payload *transaction;
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    utilization_tracker.set_active();

    transaction = peq.get_next_transaction();

    uint32_t address = transaction->get_address();
    unsigned char *data = transaction->get_data_ptr();
    unsigned int data_size = transaction->get_data_length();

    // read or write data
    if (address + data_size > mem.size()) {
      transaction->set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
      SC_LOG_ERROR(this, *transaction, "Out of bounds RAM access");
    } else {
      if (transaction->get_command() == TLM_READ_COMMAND) {
        std::memcpy(data, &mem[address], data_size);
      } else if (transaction->get_command() == TLM_WRITE_COMMAND) {
        std::memcpy(&mem[address], data, data_size);

        // set written flags
        for (unsigned int i = 0; i < data_size; ++i) {
          write_flags[address + i] = true;
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

    socket->nb_transport_bw(*transaction, phase, delay);

    wait(delay);

    transaction_done.notify(SC_ZERO_TIME);
  }
}

void RAM::process_queue() {
  while (true) {
    wait(request_issued);

    while (!requests_queue.empty()) {
      Request request = requests_queue.front();
      requests_queue.pop_front();

      tlm_generic_payload *transaction = request.transaction;
      tlm_phase phase = END_REQ;
      sc_time delay = *request.delay;

      socket->nb_transport_bw(*transaction, phase, delay);

      peq.notify(*transaction, delay);

      wait(transaction_done);
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
    request_issued.notify(SC_ZERO_TIME);

    return TLM_ACCEPTED;
  }

  return TLM_ACCEPTED;
}