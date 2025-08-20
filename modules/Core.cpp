#include "Core.h"

#include "common/protocol/ChipletExtension.h"

Core::Core(sc_module_name name, AXIUtils &axi_utils, unsigned int chip_id,
           unsigned int core_id, sc_time irq_delay)
    : sc_module(name), axi_utils(axi_utils), chip_id(chip_id), core_id(core_id),
      irq_delay(irq_delay), utilization_tracker(this->name()), request(0) {
  isocket.register_nb_transport_bw(this, &Core::nb_transport_bw);
  irq_socket.register_nb_transport_fw(this, &Core::nb_transport_fw_irq);

  SC_THREAD(core_thread);
  SC_THREAD(interrupt_thread);
}

void Core::core_thread() {
  if (thread_fn) {
    thread_fn(*this, &utilization_tracker);
  }
}

void Core::interrupt_thread() {
  tlm_generic_payload *transaction;
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  while (true) {
    wait(irq_req_evt);

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

Core::RequestHandle *
Core::send_request(tlm_command command, int request_id, int destination_id,
                   uint32_t address, bool fixed_address, bool is_volatile,
                   unsigned char *data, unsigned int data_len,
                   unsigned int axi_length, unsigned int axi_size,
                   unsigned int axi_burst) {
  auto *h = new RequestHandle();
  auto *transaction = new ChipletPayload();
  ChipletExtension *ext;
  tlm_phase phase;
  sc_time delay;
  tlm_sync_enum tlm_resp;

  transaction->set_command(command);
  transaction->set_data_ptr(data);
  transaction->set_data_length(data_len);

  transaction->set_axi_length(axi_length);
  transaction->set_axi_size(axi_size);
  transaction->set_axi_burst(axi_burst);

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

  transaction->set_core_id(core_id);
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

  h->transaction = transaction;
  request_handles[transaction] = h;

  phase = BEGIN_REQ;
  delay = SC_ZERO_TIME;

  tlm_resp = isocket->nb_transport_fw(*transaction, phase, delay);

  wait(req_evt);

  return h;
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum Core::nb_transport_fw_irq(tlm_generic_payload &transaction,
                                        tlm_phase &phase,
                                        sc_core::sc_time &delay) {
  switch (phase) {
  case BEGIN_REQ:
    delay += delays.irq_transfer(transaction);

    auto *transaction_copy =
        static_cast<ChipletPayload *>(&transaction)->clone();

    irq_queue.push_back(transaction_copy);
    irq_req_evt.notify(delay);

    phase = END_REQ;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}

tlm_sync_enum Core::nb_transport_bw(tlm_generic_payload &transaction,
                                    tlm_phase &phase, sc_core::sc_time &delay) {
  SC_LOG_DEBUG(this, transaction, "TLM Protocol: " << phase);

  switch (phase) {
  case END_REQ:
    req_evt.notify(delay);

    phase = END_REQ;
    return TLM_ACCEPTED;
  case BEGIN_RESP:
    Core::RequestHandle *h = request_handles.find(&transaction)->second;
    request_handles.erase(&transaction);

    h->notify(delay);

    phase = END_RESP;
    return TLM_COMPLETED;
  }

  return TLM_ACCEPTED;
}