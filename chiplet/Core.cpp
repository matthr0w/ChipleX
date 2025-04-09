#include "Core.h"

#include <random>

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

Core::Core(sc_module_name name) : sc_module(name), socket("socket") {
  SC_THREAD(thread);
  socket.register_nb_transport_bw(this, &Core::nb_transport_bw);
}

void Core::thread(void) {
  thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<int> delay_dist(0, 200);
  std::uniform_int_distribution<uint32_t> address_dist(0x0000, 0xFFFF);
  std::uniform_int_distribution<uint32_t> data_dist;
  std::bernoulli_distribution write_dist(0.5);

  while (true) {
    // Random delay between requests (0-200ns)
    wait(delay_dist(gen), SC_NS);

    // RAM address space: 0x0000 - 0xFFFF
    // Random address
    uint32_t address = address_dist(gen);

    // Random 4 bytes
    uint32_t *data = new uint32_t(data_dist(gen));
    unsigned int data_size = 4;

    // Read or write
    bool do_write = write_dist(gen);

    if (do_write) {
      SC_LOG_INFO(this, "Sending request: WRITE to 0x"
                            << std::hex << address << " with data 0x" << *data);

      send_request(TLM_WRITE_COMMAND, address,
                   reinterpret_cast<unsigned char *>(data), data_size);
    } else {
      SC_LOG_INFO(this, "Sending request: READ from 0x" << std::hex << address);

      send_request(TLM_READ_COMMAND, address,
                   reinterpret_cast<unsigned char *>(data), data_size);
    }
  }
}

void Core::send_request(tlm_command command, uint32_t address,
                        unsigned char *data, unsigned int data_size) {
  auto *payload = new tlm_generic_payload();
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    payload->set_command(command);
    payload->set_address(address);
    payload->set_data_ptr(data);
    payload->set_data_length(data_size);
    payload->set_response_status(TLM_INCOMPLETE_RESPONSE);

    phase = BEGIN_REQ;
    delay = SC_ZERO_TIME;

    tlm_resp = socket->nb_transport_fw(*payload, phase, delay);

    if (tlm_resp == TLM_UPDATED) {
      // Bus processes the request.
      SC_LOG_INFO(this, "Waiting for transaction...");
      wait(delay);
    } else if (tlm_resp == TLM_ACCEPTED) {
      // Bus accepted the request but is busy (queued).
      SC_LOG_INFO(this, "Waiting for transaction...");
    }

    wait(transaction_done);

    SC_LOG_INFO(this, "Transaction successful");

    break;
  }

  delete payload;
}

tlm_sync_enum Core::nb_transport_bw(tlm_generic_payload &payload,
                                    tlm_phase &phase, sc_core::sc_time &delay) {
  if (phase == BEGIN_RESP) {
    delay += sc_time(5, SC_NS); // DELAY

    transaction_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  } else {
    return TLM_ACCEPTED;
  }
}