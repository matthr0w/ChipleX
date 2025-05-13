#include "Core.h"
#include "Config.h"

#include <random>

#include "common/Delays.h"
#include "common/protocol/ChipletExtension.h"
#include "common/protocol/ChipletPayload.h"

#include "include/globals.h"
#include "include/logging.h"

chiplet::Core::Core(sc_module_name name, unsigned int chiplet_id,
                    unsigned int core_id)
    : sc_module(name), chiplet_id(chiplet_id), core_id(core_id), request(0),
      socket("socket"), irq_peq("irq_peq") {
  socket.register_nb_transport_bw(this, &chiplet::Core::nb_transport_bw);
  irq_socket.register_nb_transport_fw(this,
                                      &chiplet::Core::nb_transport_fw_irq);

  SC_THREAD(core_thread);

  SC_THREAD(handle_interrupt);
  sensitive << irq_peq.get_event();
}

void chiplet::Core::core_thread() {
  size_t total_bytes = Config::instance().ramSize() * 1024;
  size_t half_bytes = total_bytes / 2;

  // random number distributions
  thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<int> delay_dist(10, 100);

  std::bernoulli_distribution write_dist(0.5);
  std::uniform_int_distribution<uint32_t> data_dist;

  std::uniform_int_distribution<uint32_t> destination_dist(0, num_chiplets);

  std::uniform_int_distribution<uint32_t> address_onchip_dist(0,
                                                              half_bytes - 1);
  std::uniform_int_distribution<uint32_t> address_offchip_dist(half_bytes,
                                                               total_bytes - 1);

  while (true) {
    // random delay between requests
    wait(delay_dist(gen), SC_NS);

    // random destination id
    int destination_id = destination_dist(gen);

    // read or write
    bool do_write = write_dist(gen);

    // random RAM address
    uint32_t address;
    if (destination_id == chiplet_id) {
      address = address_onchip_dist(gen);
    } else {
      address = address_offchip_dist(gen);
    }

    // random 4 bytes
    uint32_t *data = new uint32_t(data_dist(gen));
    unsigned int data_size = sizeof(uint32_t);

    send_request(do_write ? TLM_WRITE_COMMAND : TLM_READ_COMMAND, request,
                 destination_id, address,
                 reinterpret_cast<unsigned char *>(data), data_size);

    request += 1;
  }
}

void chiplet::Core::handle_interrupt() {
  tlm_generic_payload *transaction;
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait();

    transaction = irq_peq.get_next_transaction();

    transaction->get_extension(ext);
    int request_id = ext->request_id;

    SC_LOG_INFO(this, *transaction, "Received IRQ from request " << request_id);

    uint32_t address = transaction->get_address();

    // 4 zero bytes
    uint32_t *data = new uint32_t(0);
    unsigned int data_size = 4;

    send_request(TLM_READ_COMMAND, request_id, chiplet_id, address,
                 reinterpret_cast<unsigned char *>(data), data_size);

    delete transaction;
  }
}

void chiplet::Core::send_request(tlm_command command, int request_id,
                                 int destination_id, uint32_t address,
                                 unsigned char *data, unsigned int data_size) {
  std::scoped_lock lock(request_mutex);

  auto *transaction = new ChipletPayload();
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  transaction->set_command(command);
  transaction->set_address(address);
  transaction->set_data_ptr(data);
  transaction->set_data_length(data_size);

  transaction->set_request_id(request_id);
  transaction->set_destination_id(destination_id);

  if (command == TLM_READ_COMMAND) {
    SC_LOG_INFO(this, *transaction,
                "Sending request: READ from 0x" << std::hex << address);
  } else if (command == TLM_WRITE_COMMAND) {
    SC_LOG_INFO(this, *transaction,
                "Sending request: WRITE to 0x"
                    << std::hex << address << " with data 0x"
                    << *reinterpret_cast<uint32_t *>(data));
  }

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

  SC_LOG_INFO(this, *transaction, "Transaction successful");

  delete transaction;
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum
chiplet::Core::nb_transport_fw_irq(tlm_generic_payload &transaction,
                                   tlm_phase &phase, sc_core::sc_time &delay) {
  if (phase == BEGIN_REQ) {
    delay += get_irq_transfer_delay(
        *this, transaction, Config::instance().interconnectProtocolClkCycle());

    auto *transaction_copy =
        static_cast<ChipletPayload *>(&transaction)->clone();

    irq_peq.notify(*transaction_copy, delay);

    phase = END_REQ;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum chiplet::Core::nb_transport_bw(tlm_generic_payload &transaction,
                                             tlm_phase &phase,
                                             sc_core::sc_time &delay) {
  if (phase == BEGIN_RESP) {
    ChipletExtension *ext;

    transaction.get_extension(ext);

    if (ext->source_id == -1) {
      delay += get_bus_transfer_bw_delay(*this, transaction,
                                         Config::instance().busClkCycle(),
                                         Config::instance().busWidth());
    } else {
      // request to interconnect
      // no direct data response -> no extra delay
      delay += SC_ZERO_TIME;
    }

    transaction_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}