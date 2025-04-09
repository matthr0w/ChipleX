#include "Core.h"

#include "sysc/kernel/sc_simcontext.h"
#include "sysc/kernel/sc_time.h"
#include "tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h"

#include <cstdint>
#include <iostream>

using namespace sc_core;
using namespace tlm;

Core::Core(sc_module_name name) : sc_module(name), socket("socket") {
  SC_THREAD(thread);
  socket.register_nb_transport_bw(this, &Core::nb_transport_bw);
}

void Core::thread(void) {
  while (true) {
    auto *payload = new tlm_generic_payload();

    // RAM address space:
    // 0x0000 - 0xFFFF
    uint64_t addr = rand() % 0x10000;
    // Random 4 bytes
    uint32_t *data = new uint32_t(rand());

    payload->set_address(addr);
    payload->set_data_ptr(reinterpret_cast<unsigned char *>(data));
    payload->set_data_length(4);

    bool do_write = rand() % 2;
    if (do_write) {
      payload->set_command(TLM_WRITE_COMMAND);
      std::cout << "[" << sc_time_stamp() << "] - " << name() << ":"
                << " Sending WRITE to 0x" << std::hex << addr << " with data 0x"
                << *data << "\n";
    } else {
      payload->set_command(TLM_READ_COMMAND);
      std::cout << "[" << sc_time_stamp() << "] - " << name() << ":"
                << " Sending READ from 0x" << std::hex << addr << "\n";
    }

    payload->set_response_status(TLM_INCOMPLETE_RESPONSE);

    sc_time delay = SC_ZERO_TIME;
    tlm_phase phase = BEGIN_REQ;

    while (true) {
      tlm_sync_enum tlm_resp;

      socket->nb_transport_fw(*payload, phase, delay);

      if (tlm_resp == TLM_UPDATED) {
        wait(delay);
      }

      wait(transactionFinished_event);

      std::cout << "[" << sc_time_stamp() << "] - " << name() << ":"
                << " Finished transaction\n";

      if (payload->get_response_status() != TLM_OK_RESPONSE) {
        continue;
      }

      break;
    }

    delete payload;
  }
}

tlm_sync_enum Core::nb_transport_bw(tlm_generic_payload &payload,
                                    tlm_phase &phase, sc_time &delay) {
  if (phase != BEGIN_RESP) {
    std::cout << sc_time_stamp() << ": '" << name() << "\tProtocol Error Core"
              << std::endl;
    exit(1);
  }

  delay += sc_time(5, SC_NS); // DELAY
  transactionFinished_event.notify(delay);

  phase = END_RESP;
  return TLM_COMPLETED;
}