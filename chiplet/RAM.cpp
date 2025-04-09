#include "RAM.h"

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

RAM::RAM(sc_module_name name)
    : sc_module(name), socket("socket"), mem(0x10000, 0), // 0x0000 - 0xFFFF
      access_time(sc_time(20, SC_NS)), peq("peq") {
  socket.register_nb_transport_fw(this, &RAM::nb_transport_fw);

  SC_THREAD(serve_bus);
  sensitive << peq.get_event();
}

tlm_sync_enum RAM::nb_transport_fw(tlm_generic_payload &payload,
                                   tlm_phase &phase, sc_time &delay) {
  if (phase != BEGIN_REQ) {
    SC_LOG_ERROR(this,
                 "Protocol Error: Request from Bus with Phase != BEGIN_REQ");
    exit(1);
  }

  auto length = payload.get_data_length();

  delay += length * access_time; // DELAY

  peq.notify(payload, delay);
  payload.set_response_status(TLM_OK_RESPONSE);

  phase = END_REQ;
  return TLM_UPDATED;
}

void RAM::serve_bus() {
  tlm_sync_enum tlm_resp;
  tlm_generic_payload *payload;
  sc_time delay;
  tlm_phase phase;

  while (true) {
    wait();

    payload = peq.get_next_transaction();

    sc_dt::uint64 addr = payload->get_address();
    unsigned char *ptr = payload->get_data_ptr();
    unsigned int data_size = payload->get_data_length();

    // read or write data
    if (addr + data_size > mem.size()) {
      payload->set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
    } else {
      if (payload->get_command() == TLM_READ_COMMAND)
        std::memcpy(ptr, &mem[addr], data_size);
      else if (payload->get_command() == TLM_WRITE_COMMAND)
        std::memcpy(&mem[addr], ptr, data_size);

      payload->set_response_status(TLM_OK_RESPONSE);
    }

    payload->set_command(TLM_IGNORE_COMMAND);

    phase = BEGIN_RESP;
    delay = SC_ZERO_TIME;

    tlm_resp = socket->nb_transport_bw(*payload, phase, delay);

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
    }
  }
}