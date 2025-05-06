#include "RAM.h"
#include "Config.h"

#include "common/Delays.h"

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

fpga::RAM::RAM(sc_module_name name)
    : sc_module(name), socket("socket"), mem(RAM_SIZE * 1024, 0), peq("peq") {
  socket.register_nb_transport_fw(this, &fpga::RAM::nb_transport_fw);

  SC_THREAD(process_transaction);
  sensitive << peq.get_event();
}

void fpga::RAM::process_transaction() {
  tlm_generic_payload *transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq.get_next_transaction();

    uint32_t address = transaction->get_address();
    unsigned char *data = transaction->get_data_ptr();
    unsigned int data_size = transaction->get_data_length();

    // read or write data
    if (address + data_size > mem.size()) {
      transaction->set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
    } else {
      if (transaction->get_command() == TLM_READ_COMMAND)
        std::memcpy(data, &mem[address], data_size);
      else if (transaction->get_command() == TLM_WRITE_COMMAND)
        std::memcpy(&mem[address], data, data_size);

      transaction->set_response_status(TLM_OK_RESPONSE);
    }

    // RAM access delay
    wait(get_mem_access_delay(*this, *transaction, RAM_CLK_CYCLE,
                              RAM_ACCESS_DELAY, RAM_WIDTH));

    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    tlm_resp = socket->nb_transport_bw(*transaction, phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
    }
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum fpga::RAM::nb_transport_fw(tlm_generic_payload &transaction,
                                         tlm_phase &phase, sc_time &delay) {
  if (phase != BEGIN_REQ) {
    SC_LOG_ERROR(this, transaction,
                 "Protocol Error: Request from Bus with Phase != BEGIN_REQ");
    exit(1);
  }

  delay +=
      get_bus_transfer_fw_delay(*this, transaction, BUS_CLK_CYCLE, BUS_WIDTH);

  peq.notify(transaction, delay);

  phase = END_REQ;
  return TLM_UPDATED;
}