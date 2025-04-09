#include "Core.h"

#include "sysc/kernel/sc_simcontext.h"
#include "sysc/kernel/sc_time.h"
#include "tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h"

#include <cstdint>
#include <iostream>
#include <ostream>

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

Core::Core(sc_module_name name) : sc_module(name), socket("socket") {
  SC_THREAD(thread);
  socket.register_nb_transport_bw(this, &Core::nb_transport_bw);
}

void Core::thread(void) {
  while (true) {
    // RAM address space:
    // 0x0000 - 0xFFFF
    uint32_t address = rand() % 0x10000;
    // Random 4 bytes
    uint32_t *data = new uint32_t(rand());
    unsigned int data_size = 4;

    // Read or write
    bool do_write = rand() % 2;

    if (do_write) {
      SC_LOG_INFO(this, "Sending Request: WRITE to 0x"
                            << std::hex << address << " with data 0x" << *data);

      send_request(TLM_WRITE_COMMAND, address,
                   reinterpret_cast<unsigned char *>(data), data_size);
    } else {
      SC_LOG_INFO(this, "Sending Request: READ from 0x"
                            << std::hex << address << " with data 0x" << *data);

      send_request(TLM_READ_COMMAND, address,
                   reinterpret_cast<unsigned char *>(data), data_size);
    }
  }
}

void Core::send_request(tlm_command command, uint32_t address,
                        unsigned char *data, unsigned int data_size) {
  auto *payload = new tlm_generic_payload();
  sc_time delay = SC_ZERO_TIME;
  tlm_phase phase = BEGIN_REQ;
  tlm_sync_enum tlm_resp;

  while (true) {
    payload->set_command(TLM_WRITE_COMMAND);
    payload->set_address(address);
    payload->set_data_ptr(data);
    payload->set_data_length(data_size);
    payload->set_response_status(TLM_INCOMPLETE_RESPONSE);

    tlm_resp = socket->nb_transport_fw(*payload, phase, delay);

    if (tlm_resp == TLM_UPDATED) {
      wait(delay);
    }

    SC_LOG_INFO(this, "Request granted");

    wait(transactionFinished_event);

    SC_LOG_INFO(this, "Transaction finished");

    if (payload->get_response_status() != TLM_OK_RESPONSE) {
      continue;
    }

    break;
  }

  delete payload;
}

tlm_sync_enum Core::nb_transport_bw(tlm_generic_payload &payload,
                                    tlm_phase &phase, sc_time &delay) {
  if (phase != BEGIN_RESP) {
    SC_LOG_ERROR(this,
                 "Protocol Error: Response from Bus with Phase != BEGIN_RESP");
    exit(1);
  }

  delay += sc_time(5, SC_NS); // DELAY
  transactionFinished_event.notify(delay);

  phase = END_RESP;
  return TLM_COMPLETED;
}