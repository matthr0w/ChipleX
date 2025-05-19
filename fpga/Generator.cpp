#include "Generator.h"
#include "Config.h"

#include <random>

#include "chiplet/Config.h"

#include "common/Delays.h"
#include "common/protocol/ChipletExtension.h"
#include "common/protocol/ChipletPayload.h"

#include "include/globals.h"
#include "include/logging.h"

fpga::Generator::Generator(sc_module_name name, unsigned int fpga_id)
    : sc_module(name), fpga_id(fpga_id), request(0), socket("socket"),
      irq_peq("irq_peq") {
  socket.register_nb_transport_bw(this, &fpga::Generator::nb_transport_bw);
  irq_socket.register_nb_transport_fw(this,
                                      &fpga::Generator::nb_transport_fw_irq);

  SC_THREAD(gen_thread);

  SC_THREAD(handle_interrupt);
  sensitive << irq_peq.get_event();
}

void fpga::Generator::gen_thread() {
  if (gen_fn) {
    gen_fn(*this);
  }
}

void fpga::Generator::handle_interrupt() {
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

    if (interrupt_fn) {
      interrupt_fn(*this, transaction);
    }

    delete transaction;
  }
}

void fpga::Generator::send_random(unsigned int delay, double write_prob,
                                  unsigned int destination_min,
                                  unsigned int destination_max,
                                  size_t data_size) {
  if (destination_max > num_chiplets) {
    destination_max = num_chiplets;
  }

  // address space separation
  size_t total_bytes_fpga = fpga::Config::instance().ramSize() * 1024;
  size_t half_bytes_fpga = total_bytes_fpga / 2;
  size_t total_bytes_chiplet = chiplet::Config::instance().ramSize() * 1024;
  size_t half_bytes_chiplet = total_bytes_chiplet / 2;

  // random number distributions
  thread_local std::mt19937 gen(std::random_device{}());

  std::bernoulli_distribution write_dist(write_prob);

  std::uniform_int_distribution<uint32_t> destination_dist(destination_min,
                                                           destination_max);

  std::uniform_int_distribution<uint32_t> address_onchip_dist(
      0, half_bytes_fpga - 1);
  std::uniform_int_distribution<uint32_t> address_offchip_dist(
      half_bytes_chiplet, total_bytes_chiplet - 1);

  std::uniform_int_distribution<unsigned short> byte_dist(0, 255);
  while (true) {
    wait(delay, SC_NS);

    // read or write
    bool do_write = write_dist(gen);

    // random destination id
    uint32_t destination_id = destination_dist(gen);

    // random RAM address
    uint32_t address;

    if (destination_id == 0) {
      address = address_onchip_dist(gen);
    } else {
      address = address_offchip_dist(gen);
    }

    // random data buffer
    unsigned char *data = new unsigned char[data_size];
    for (size_t i = 0; i < data_size; ++i) {
      data[i] = static_cast<unsigned char>(byte_dist(gen));
    }

    send_request(do_write ? TLM_WRITE_COMMAND : TLM_READ_COMMAND, request,
                 destination_id, address, data, data_size);

    request += 1;
  }
}

void fpga::Generator::send_request(tlm_command command, int request_id,
                                   int destination_id, uint32_t address,
                                   unsigned char *data,
                                   unsigned int data_size) {
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
                "Sending request: WRITE to 0x" << std::hex << address);
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
fpga::Generator::nb_transport_fw_irq(tlm_generic_payload &transaction,
                                     tlm_phase &phase,
                                     sc_core::sc_time &delay) {
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

tlm_sync_enum fpga::Generator::nb_transport_bw(tlm_generic_payload &transaction,
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