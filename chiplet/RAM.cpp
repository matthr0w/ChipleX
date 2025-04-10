#include "RAM.h"
#include "Delays.h"

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

RAM::RAM(sc_module_name name)
    : sc_module(name), socket("socket"), mem(0x10000, 0), // 0x0000 - 0xFFFF
      peq("peq") {
  socket.register_nb_transport_fw(this, &RAM::nb_transport_fw);

  SC_THREAD(process_transaction);
  sensitive << peq.get_event();
}

void RAM::process_transaction() {
  tlm_generic_payload *transaction;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = peq.get_next_transaction();

    sc_dt::uint64 address = transaction->get_address();
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

    wait(get_mem_access_delay(
        transaction->get_data_length())); // RAM access delay

    transaction->set_command(TLM_IGNORE_COMMAND);

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
tlm_sync_enum RAM::nb_transport_fw(tlm_generic_payload &transaction,
                                   tlm_phase &phase, sc_time &delay) {
  if (phase != BEGIN_REQ) {
    SC_LOG_ERROR(this,
                 "Protocol Error: Request from Bus with Phase != BEGIN_REQ");
    exit(1);
  }

  delay += get_bus_transfer_delay(transaction.get_data_length());

  peq.notify(transaction, delay);

  phase = END_REQ;
  return TLM_UPDATED;
}