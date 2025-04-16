#include "Core.h"
#include "Delays.h"

#include <random>

#include "include/logging.h"
#include "include/payload.h"
#include "include/payload_extension.h"

using namespace sc_core;
using namespace tlm;

Core::Core(sc_module_name name, unsigned int chiplet_id, unsigned int core_id)
    : sc_module(name), chiplet_id(chiplet_id), core_id(core_id), request(0),
      socket("socket") {
  socket.register_nb_transport_bw(this, &Core::nb_transport_bw);
  SC_THREAD(run_core);
}

void Core::run_core() {
  // random number distributions
  thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<int> delay_dist(0, 100);
  std::uniform_int_distribution<uint32_t> address_dist(0x0000, 0xFFFF);
  std::uniform_int_distribution<uint32_t> data_dist;
  std::bernoulli_distribution write_dist(0.5);

  while (true) {
    // random delay between requests
    wait(delay_dist(gen), SC_NS);

    // RAM address spaces:
    // 0x000000 - 0x00FFFF RAM Chiplet 0
    // 0x010000 - 0x01FFFF RAM Chiplet 1
    // ...

    // random RAM address
    uint32_t address = address_dist(gen);

    // random 4 bytes
    uint32_t *data = new uint32_t(data_dist(gen));
    unsigned int data_size = 4;

    // read or write
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

    request += 1;
  }
}

void Core::send_request(tlm_command command, uint32_t address,
                        unsigned char *data, unsigned int data_size) {
  auto *transaction = new payload();
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    transaction->set_command(command);
    transaction->set_address(address);
    transaction->set_data_ptr(data);
    transaction->set_data_length(data_size);

    transaction->set_request_id(request);
    transaction->set_chiplet_id(chiplet_id);
    transaction->set_core_id(core_id);
    // for now: always send to other chiplet
    transaction->set_destination_id((chiplet_id == 0) ? 1 : 0);

    phase = BEGIN_REQ;
    delay = SC_ZERO_TIME;

    tlm_resp = socket->nb_transport_fw(*transaction, phase, delay);

    if (tlm_resp == TLM_UPDATED) {
      // bus processes the request
      wait(delay);
    } else if (tlm_resp == TLM_ACCEPTED) {
      // bus accepted the request but is busy (queued)
    }

    wait(transaction_done);

    SC_LOG_INFO(this, "Transaction successful");

    break;
  }

  delete transaction;
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum Core::nb_transport_bw(tlm_generic_payload &transaction,
                                    tlm_phase &phase, sc_core::sc_time &delay) {
  if (phase == BEGIN_RESP) {
    delay += get_bus_transfer_delay(transaction.get_data_length());

    transaction_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  } else if (phase == END_REQ) {
    payload_extension *ext;
    transaction.get_extension(ext);

    if (ext->destination_id != chiplet_id) {
      transaction_done.notify(delay);
    }

    return TLM_ACCEPTED;
  }
  return TLM_ACCEPTED;
}