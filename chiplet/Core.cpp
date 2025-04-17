#include "Core.h"
#include "Delays.h"

#include <random>

#include "include/logging.h"
#include "include/payload.h"
#include "include/payload_extension.h"

using namespace sc_core;
using namespace tlm;

Core::Core(sc_module_name name, unsigned int chiplet_id, unsigned int core_id)
    : sc_module(name), chiplet_id(chiplet_id), core_id(core_id), running(false),
      request(0), socket("socket"), irq_peq("irq_peq") {
  socket.register_nb_transport_bw(this, &Core::nb_transport_bw);
  irq_socket.register_nb_transport_fw(this, &Core::nb_transport_fw_irq);

  SC_THREAD(core_thread);

  SC_THREAD(handle_interrupt);
  sensitive << irq_peq.get_event();
}

void Core::core_thread() {
  // random number distributions
  thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<int> delay_dist(0, 100);
  std::uniform_int_distribution<uint32_t> address_dist(0x0000, 0xFFFF);
  std::uniform_int_distribution<uint32_t> data_dist;
  std::bernoulli_distribution write_dist(0.5);

  while (true) {
    // random delay between requests
    wait(delay_dist(gen), SC_NS);

    if (running) {
      continue;
    }

    running = true;

    // RAM address spaces:
    // 0x000000 - 0x00FFFF RAM Chiplet 0
    // 0x010000 - 0x01FFFF RAM Chiplet 1
    // ...

    // random RAM address
    uint32_t address = address_dist(gen);

    // random 4 bytes
    uint32_t *data = new uint32_t(data_dist(gen));
    unsigned int data_size = 4;

    // for now: always send to other chiplet
    int destination_id = (chiplet_id == 0) ? 1 : 0;

    // read or write
    bool do_write = write_dist(gen);

    if (do_write) {
      SC_LOG_INFO(this, "Sending request: WRITE to 0x"
                            << std::hex << address << " with data 0x" << *data);

      send_request(TLM_WRITE_COMMAND, request, destination_id, address,
                   reinterpret_cast<unsigned char *>(data), data_size);
    } else {
      SC_LOG_INFO(this, "Sending request: READ from 0x" << std::hex << address);

      send_request(TLM_READ_COMMAND, request, destination_id, address,
                   reinterpret_cast<unsigned char *>(data), data_size);
    }

    request += 1;

    running = false;
  }
}

void Core::handle_interrupt() {
  tlm_generic_payload *transaction;
  payload_extension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    running = true;

    transaction = irq_peq.get_next_transaction();

    transaction->get_extension(ext);
    int request_id = ext->request_id;

    SC_LOG_WARN(this, "IRQ received from request " << request_id);

    uint32_t address = transaction->get_address();

    // 4 zero bytes
    uint32_t *data = new uint32_t(0);
    unsigned int data_size = 4;

    SC_LOG_INFO(this, "Sending request: READ from 0x" << std::hex << address);

    send_request(TLM_READ_COMMAND, request_id, chiplet_id, address,
                 reinterpret_cast<unsigned char *>(data), data_size);

    delete transaction;

    running = false;
  }
}

void Core::send_request(tlm_command command, int request_id, int destination_id,
                        uint32_t address, unsigned char *data,
                        unsigned int data_size) {
  auto *transaction = new payload();
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    transaction->set_command(command);
    transaction->set_address(address);
    transaction->set_data_ptr(data);
    transaction->set_data_length(data_size);

    transaction->set_request_id(request_id);
    transaction->set_source_id(chiplet_id);
    transaction->set_core_id(core_id);
    transaction->set_destination_id(destination_id);

    phase = BEGIN_REQ;
    delay = SC_ZERO_TIME;

    SC_DUMP_TRANS(this, *transaction);

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
tlm_sync_enum Core::nb_transport_fw_irq(tlm_generic_payload &transaction,
                                        tlm_phase &phase,
                                        sc_core::sc_time &delay) {
  if (phase == BEGIN_REQ) {
    delay += SC_ZERO_TIME; // TODO: add delay

    if (running) {
      wait(transaction_done);
    }

    auto *transaction_copy = static_cast<payload *>(&transaction)->clone();
    irq_peq.notify(*transaction_copy, delay);

    phase = END_REQ;
    return TLM_COMPLETED;
  }
  return TLM_ACCEPTED;
}

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

    // transaction is done if request was to interconnect
    // -> no handshake to other chiplets
    if (ext->destination_id != chiplet_id) {
      transaction_done.notify(delay);
      return TLM_COMPLETED;
    }

    return TLM_ACCEPTED;
  }
  return TLM_ACCEPTED;
}