#include "Core.h"
#include "Delays.h"

#include <random>

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

Core::Core(sc_module_name name, unsigned int chiplet_instances, unsigned int id)
    : sc_module(name), chiplet_instances(chiplet_instances), id(id),
      socket("socket") {
  socket.register_nb_transport_bw(this, &Core::nb_transport_bw);
  SC_THREAD(run_core);
}

void Core::run_core() {
  thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<int> delay_dist(0, 20);
  std::uniform_int_distribution<int> chiplet_dist(0, chiplet_instances - 1);
  std::uniform_int_distribution<uint32_t> address_dist(0x0000, 0xFFFF);
  std::uniform_int_distribution<uint32_t> data_dist;
  std::bernoulli_distribution write_dist(0.5);

  while (true) {
    // random delay between requests (0-200ns)
    wait(delay_dist(gen), SC_NS);

    // RAM address space: 0x0000 - 0xFFFF + 0x010000 - 0xCHIPLETS0000 for
    // chiplet selection other chiplet random address
    unsigned int forwarder = (id == 0) ? 1 : 0;
    uint32_t address = forwarder * 0x010000 + address_dist(gen);

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
  }
}

void Core::send_request(tlm_command command, uint32_t address,
                        unsigned char *data, unsigned int data_size) {
  auto *transaction = new tlm_generic_payload();
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    transaction->set_command(command);
    transaction->set_address(address);
    transaction->set_data_ptr(data);
    transaction->set_data_length(data_size);

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

  delete data;
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
    uint32_t address = transaction.get_address();
    unsigned int destination = address >> 16;

    if (destination != id) {
      transaction_done.notify(delay);
    }

    return TLM_ACCEPTED;
  }
  return TLM_ACCEPTED;
}