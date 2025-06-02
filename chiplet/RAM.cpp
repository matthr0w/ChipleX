#include "RAM.h"

#include "common/Delays.h"
#include "common/Tracker.h"
#include "common/protocol/ChipletExtension.h"

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

chiplet::RAM::RAM(sc_module_name name)
    : sc_module(name), utilization_tracker(this->name()), socket("socket"),
      peq("peq"), mem(ram_size * 1024, 0) {
  socket.register_nb_transport_fw(this, &chiplet::RAM::nb_transport_fw);

  SC_THREAD(process_transaction);
  sensitive << peq.get_event();
}

void chiplet::RAM::process_transaction() {
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
      }
    }

    // RAM access delay
    wait(get_mem_access_delay(*this, *transaction, ram_clk_cycle,
                              ram_access_delay, ram_width));

    // off-chip requests: request done on last flit
    if (transaction->get_command() == TLM_WRITE_COMMAND) {
      transaction->get_extension(ext);
      if (ext->flit_id == ext->flit_count - 1) {
        sc_time latency = sc_time_stamp() - ext->start_time;
        LatencyTracker::instance().record(latency);
      }
    }

    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    tlm_resp = socket->nb_transport_bw(*transaction, phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
    }

    utilization_tracker.set_idle();
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum chiplet::RAM::nb_transport_fw(tlm_generic_payload &transaction,
                                            tlm_phase &phase, sc_time &delay) {
  if (phase == BEGIN_REQ) {
    delay +=
        get_mem_address_assignment_delay(*this, transaction, ram_address_delay);

    peq.notify(transaction, delay);

    phase = END_REQ;
    return TLM_UPDATED;
  }

  return TLM_ACCEPTED;
}