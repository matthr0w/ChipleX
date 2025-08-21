#include "Core.h"

#include "common/protocol/ChipletExtension.h"

Core::Core(sc_module_name name, AXIUtils &axi_utils, unsigned int chip_id,
           unsigned int core_id, sc_time irq_delay)
    : sc_module(name), axi_utils(axi_utils), chip_id(chip_id), core_id(core_id),
      irq_delay(irq_delay), utilization_tracker(this->name()) {
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

// -------------------------------------------------------
// AXI methods
// -------------------------------------------------------
Core::RequestHandle *Core::read_internal(int request_id, int destination_id,
                                         uint32_t address, bool fixed_address,
                                         unsigned char *data,
                                         unsigned int data_length,
                                         unsigned int burst_type,
                                         bool is_volatile) {
  auto *handle = new RequestHandle();
  auto *transaction = new ChipletPayload();

  transaction->set_command(TLM_READ_COMMAND);
  transaction->set_address(address);
  transaction->set_fixed_address(fixed_address);
  transaction->set_data_ptr(data, false);
  transaction->set_data_length(data_length);
  transaction->set_volatile(is_volatile);

  transaction->set_core_id(core_id);
  transaction->set_request_id(request_id);
  transaction->set_destination_id(destination_id);

  switch (burst_type) {
  case 0:
    axi_utils.set_burst_fixed(transaction, data_length);
    break;
  case 1:
    axi_utils.set_burst_incr(transaction, data_length);
    break;
  case 2:
    axi_utils.set_burst_wrap(transaction, data_length);
    break;
  }

  SC_LOG_INFO(this, *transaction,
              "Sending request: READ from 0x" << std::hex << address);

  handle->transaction = transaction;
  request_handles[transaction] = handle;

  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;

  isocket->nb_transport_fw(*transaction, phase, delay);

  wait(req_evt);

  return handle;
}

Core::RequestHandle *Core::read(int request_id, int destination_id,
                                uint32_t address, unsigned char *data,
                                unsigned int data_length, bool is_volatile) {
  bool fixed_address = true;
  unsigned int burst_type = 1;
  return read_internal(request_id, destination_id, address, fixed_address, data,
                       data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::read_fixed(int request_id, int destination_id,
                                      uint32_t address, unsigned char *data,
                                      unsigned int data_length,
                                      bool is_volatile) {
  bool fixed_address = true;
  unsigned int burst_type = 0;
  return read_internal(request_id, destination_id, address, fixed_address, data,
                       data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::read_wrap(int request_id, int destination_id,
                                     uint32_t address, unsigned char *data,
                                     unsigned int data_length,
                                     bool is_volatile) {
  bool fixed_address = true;
  unsigned int burst_type = 2;
  return read_internal(request_id, destination_id, address, fixed_address, data,
                       data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::write_internal(int request_id, int destination_id,
                                          uint32_t address, bool fixed_address,
                                          unsigned char *data,
                                          unsigned int data_length,
                                          unsigned int burst_type,
                                          bool is_volatile) {
  auto *handle = new RequestHandle();
  auto *transaction = new ChipletPayload();

  transaction->set_command(TLM_WRITE_COMMAND);
  transaction->set_address(address);
  transaction->set_fixed_address(fixed_address);
  transaction->set_data_ptr(data, false);
  transaction->set_data_length(data_length);
  transaction->set_volatile(is_volatile);

  transaction->set_core_id(core_id);
  transaction->set_request_id(request_id);
  transaction->set_destination_id(destination_id);

  switch (burst_type) {
  case 0:
    axi_utils.set_burst_fixed(transaction, data_length);
    break;
  case 1:
    axi_utils.set_burst_incr(transaction, data_length);
    break;
  case 2:
    axi_utils.set_burst_wrap(transaction, data_length);
    break;
  }

  if (fixed_address) {
    SC_LOG_INFO(this, *transaction,
                "Sending request: WRITE to 0x" << std::hex << address);
  } else {
    SC_LOG_INFO(this, *transaction,
                "Sending request: WRITE to dynamic address");
  }

  handle->transaction = transaction;
  request_handles[transaction] = handle;

  tlm_phase phase = BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;

  isocket->nb_transport_fw(*transaction, phase, delay);

  wait(req_evt);

  return handle;
}

Core::RequestHandle *Core::write(int request_id, int destination_id,
                                 uint32_t address, unsigned char *data,
                                 unsigned int data_length, bool is_volatile) {
  bool fixed_address = true;
  unsigned int burst_type = 1;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::write(int request_id, int destination_id,
                                 unsigned char *data,
                                 unsigned int data_length) {
  uint32_t address = 0x0;
  bool fixed_address = false;
  unsigned int burst_type = 1;
  bool is_volatile = true;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::write_fixed(int request_id, int destination_id,
                                       uint32_t address, unsigned char *data,
                                       unsigned int data_length,
                                       bool is_volatile) {
  bool fixed_address = true;
  unsigned int burst_type = 0;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::write_fixed(int request_id, int destination_id,
                                       unsigned char *data,
                                       unsigned int data_length) {
  uint32_t address = 0x0;
  bool fixed_address = false;
  unsigned int burst_type = 0;
  bool is_volatile = true;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::write_wrap(int request_id, int destination_id,
                                      uint32_t address, unsigned char *data,
                                      unsigned int data_length,
                                      bool is_volatile) {
  bool fixed_address = true;
  unsigned int burst_type = 2;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, burst_type, is_volatile);
}

Core::RequestHandle *Core::write_wrap(int request_id, int destination_id,
                                      unsigned char *data,
                                      unsigned int data_length) {
  uint32_t address = 0x0;
  bool fixed_address = false;
  unsigned int burst_type = 2;
  bool is_volatile = true;
  return write_internal(request_id, destination_id, address, fixed_address,
                        data, data_length, burst_type, is_volatile);
}