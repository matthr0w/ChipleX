#include "Generator.h"

#include <random>

#include "common/Delays.h"
#include "common/Tracker.h"
#include "common/protocol/ChipletExtension.h"
#include "common/protocol/ChipletPayload.h"

#include "include/globals.h"
#include "include/logging.h"

fpga::Generator::Generator(sc_module_name name, unsigned int fpga_id)
    : sc_module(name), utilization_tracker(this->name()), fpga_id(fpga_id),
      request(0), socket("socket") {
  socket.register_nb_transport_bw(this, &fpga::Generator::nb_transport_bw);
  irq_socket.register_nb_transport_fw(this,
                                      &fpga::Generator::nb_transport_fw_irq);

  SC_THREAD(gen_thread);
  SC_THREAD(interrupt_thread);
}

void fpga::Generator::gen_thread() {
  if (gen_fn) {
    gen_fn(*this, &utilization_tracker);
  }
}

void fpga::Generator::interrupt_thread() {
  tlm_generic_payload *transaction;
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait(irq_event);

    while (!irq_queue.empty()) {
      transaction = irq_queue.front();
      irq_queue.pop_front();

      if (interrupt_fn) {
        interrupt_fn(*this, &utilization_tracker, transaction);
      }

      delete transaction;
    }
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
  size_t total_bytes_fpga = fpga_ram_size * 1024;
  size_t half_bytes_fpga = total_bytes_fpga / 2;
  size_t total_bytes_chiplet = chiplet_ram_size * 1024;
  size_t half_bytes_chiplet = total_bytes_chiplet / 2;

  // random number distributions
  thread_local std::mt19937 gen(std::random_device{}());

  std::bernoulli_distribution write_dist(write_prob);

  std::uniform_int_distribution<uint32_t> destination_dist(destination_min,
                                                           destination_max);

  std::uniform_int_distribution<uint32_t> address_dist_fpga(
      0, half_bytes_fpga - data_size - 1);
  std::uniform_int_distribution<uint32_t> address_dist_chiplet(
      0, half_bytes_chiplet - data_size - 1);

  std::uniform_int_distribution<unsigned short> byte_dist(0, 255);
  while (true) {
    wait(delay, SC_NS);

    utilization_tracker.set_active();

    // read or write
    bool do_write = write_dist(gen);

    // random destination id
    uint32_t destination_id = destination_dist(gen);

    // random RAM address
    uint32_t address;

    if (destination_id == 0) {
      address = address_dist_fpga(gen);
    } else {
      address = address_dist_chiplet(gen);
    }

    // random data buffer
    unsigned char *data = new unsigned char[data_size];
    for (size_t i = 0; i < data_size; ++i) {
      data[i] = static_cast<unsigned char>(byte_dist(gen));
    }

    auto response =
        send_request(do_write ? TLM_WRITE_COMMAND : TLM_READ_COMMAND, request,
                     destination_id, address, true, false, data, data_size);

    delete response;

    request += 1;

    utilization_tracker.set_idle();
  }
}

ChipletPayload *
fpga::Generator::send_request(tlm_command command, int request_id,
                              int destination_id, uint32_t address,
                              bool fixed_address, bool is_volatile,
                              unsigned char *data, unsigned int data_size) {
  std::scoped_lock lock(request_mutex);

  auto *transaction = new ChipletPayload();
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  transaction->set_command(command);
  transaction->set_data_ptr(data);
  transaction->set_data_length(data_size);

  transaction->set_fixed_address(fixed_address);
  transaction->set_volatile(is_volatile);

  if (command == TLM_WRITE_COMMAND) {
    if (fixed_address) {
      transaction->set_address(address);
    } else {
      transaction->set_address(0x0);
    }
  } else if (command == TLM_READ_COMMAND) {
    transaction->set_address(address);
  }

  transaction->set_request_id(request_id);
  transaction->set_destination_id(destination_id);

  if (command == TLM_READ_COMMAND) {
    SC_LOG_INFO(this, *transaction,
                "Sending request: READ from 0x" << std::hex << address);
  } else if (command == TLM_WRITE_COMMAND) {
    if (fixed_address) {
      SC_LOG_INFO(this, *transaction,
                  "Sending request: WRITE to 0x" << std::hex << address);
    } else {
      SC_LOG_INFO(this, *transaction,
                  "Sending request: WRITE to dynamic address");
    }
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

  // on-chip requests: request done
  if (destination_id == fpga_id) {
    transaction->get_extension(ext);
    sc_time latency = sc_time_stamp() - ext->start_time;
    LatencyTracker::instance().record(latency);
  }

  SC_LOG_INFO(this, *transaction, "Transaction successful");

  return transaction;
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum
fpga::Generator::nb_transport_fw_irq(tlm_generic_payload &transaction,
                                     tlm_phase &phase,
                                     sc_core::sc_time &delay) {
  if (phase == BEGIN_REQ) {
    delay += get_irq_transfer_delay(*this, transaction, irq_delay);

    auto *transaction_copy =
        static_cast<ChipletPayload *>(&transaction)->clone();

    irq_queue.push_back(transaction_copy);
    irq_event.notify(delay);

    phase = END_REQ;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum fpga::Generator::nb_transport_bw(tlm_generic_payload &transaction,
                                               tlm_phase &phase,
                                               sc_core::sc_time &delay) {
  if (phase == BEGIN_RESP) {
    transaction_done.notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}