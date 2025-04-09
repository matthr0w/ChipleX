#include "Bus.h"

#include <iostream>

using namespace sc_core;
using namespace tlm;

Bus::Bus(sc_module_name name)
    : sc_module(name), target_socket_core1("target_socket_core1"),
      target_socket_core2("target_socket_core2"),
      initiator_socket("initiator_socket"), peq1("peq_core1"),
      peq2("peq_core2"), cycle(sc_time(10, SC_NS)), current_owner(CORE1),
      arbitration_delay(sc_time(5, SC_NS)) {

  target_socket_core1.register_nb_transport_fw(this,
                                               &Bus::nb_transport_fw_core1);
  target_socket_core2.register_nb_transport_fw(this,
                                               &Bus::nb_transport_fw_core2);
  initiator_socket.register_nb_transport_bw(this, &Bus::nb_transport_bw);

  SC_THREAD(serve_core1);
  sensitive << peq1.get_event();
  SC_THREAD(serve_core2);
  sensitive << peq2.get_event();
}

void Bus::set_address_range(uint64_t base, uint64_t size) {
  base_addr = base;
  addr_size = size;
}

void Bus::set_bus_frequency(double mhz) { cycle = sc_time(1.0 / mhz, SC_US); }

tlm_sync_enum Bus::nb_transport_fw_core1(tlm_generic_payload &payload,
                                         tlm_phase &phase, sc_time &delay) {
  if (phase != BEGIN_REQ) {
    std::cout << sc_time_stamp() << ": '" << name()
              << "\tProtocol Error Bus Core1" << std::endl;
    exit(1);
  }

  // Wait for arbitration if bus is busy
  std::cout << "[" << sc_time_stamp() << "] - " << name()
            << ": Current Bus Owner - Core" << current_owner << "\n";

  if (current_owner != NONE && current_owner != CORE1) {
    wait(arbitration_completed);
  }

  // Arbitrate access
  if (current_owner == NONE) {
    current_owner = CORE1;
    delay += arbitration_delay; // DELAY
  }

  auto length = payload.get_data_length();
  delay += sc_time(length * 10, SC_NS); // DELAY

  peq1.notify(payload, delay);
  payload.set_response_status(TLM_OK_RESPONSE);

  phase = END_REQ;
  return TLM_UPDATED;
}

tlm_sync_enum Bus::nb_transport_fw_core2(tlm_generic_payload &payload,
                                         tlm_phase &phase, sc_time &delay) {
  if (phase != BEGIN_REQ) {
    std::cout << sc_time_stamp() << ": '" << name()
              << "\tProtocol Error Bus Core2" << std::endl;
    exit(1);
  }

  // Wait for arbitration if bus is busy
  std::cout << "[" << sc_time_stamp() << "] - " << name()
            << ": Current Bus Owner - Core" << current_owner << "\n";

  if (current_owner != NONE && current_owner != CORE2) {
    wait(arbitration_completed);
  }

  // Arbitrate access
  if (current_owner == NONE) {
    current_owner = CORE2;
    delay += arbitration_delay; // DELAY
  }

  auto length = payload.get_data_length();
  delay += sc_time(length * 10, SC_NS); // DELAY

  peq2.notify(payload, delay);
  payload.set_response_status(TLM_OK_RESPONSE);

  phase = END_REQ;
  return TLM_UPDATED;
}

tlm_sync_enum Bus::nb_transport_bw(tlm_generic_payload &payload,
                                   tlm_phase &phase, sc_time &delay) {
  if (phase != BEGIN_RESP) {
    std::cout << sc_time_stamp() << ": '" << name()
              << "\tProtocol Error Bus RAM" << std::endl;
    exit(1);
  }

  auto length = payload.get_data_length();
  delay += arbitration_delay; // DELAY

  // Notify the correct PEQ
  if (current_owner == CORE1) {
    peq1.notify(payload, delay);
  } else {
    peq2.notify(payload, delay);
  }

  phase = END_RESP;
  return TLM_COMPLETED;
}

void Bus::serve_core1() {
  tlm_sync_enum tlm_resp;
  tlm_generic_payload *payload;
  sc_time delay;
  tlm_phase phase;

  while (true) {
    wait();

    payload = peq1.get_next_transaction();

    // Forward path (to RAM)
    if (payload->get_command() == TLM_READ_COMMAND ||
        payload->get_command() == TLM_WRITE_COMMAND) {
      phase = BEGIN_REQ;
      delay = SC_ZERO_TIME;
      tlm_resp = initiator_socket->nb_transport_fw(*payload, phase, delay);
    }
    // Backward path (to core1)
    else {
      phase = BEGIN_RESP;
      delay = SC_ZERO_TIME;
      tlm_resp = target_socket_core1->nb_transport_bw(*payload, phase, delay);
    }

    if (tlm_resp == TLM_UPDATED) {
      wait(delay);
    }

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
      // Release the bus
      arbitration_completed.notify(SC_ZERO_TIME);
    }
  }
}

void Bus::serve_core2() {
  tlm_sync_enum tlm_resp;
  tlm_generic_payload *payload;
  sc_time delay;
  tlm_phase phase;

  while (true) {
    wait();

    payload = peq2.get_next_transaction();

    // Forward path (to RAM)
    if (payload->get_command() == TLM_READ_COMMAND ||
        payload->get_command() == TLM_WRITE_COMMAND) {
      phase = BEGIN_REQ;
      delay = SC_ZERO_TIME;
      tlm_resp = initiator_socket->nb_transport_fw(*payload, phase, delay);
    }
    // Backward path (to core2)
    else {
      phase = BEGIN_RESP;
      delay = SC_ZERO_TIME;
      tlm_resp = target_socket_core2->nb_transport_bw(*payload, phase, delay);
    }

    if (tlm_resp == TLM_UPDATED) {
      wait(delay);
    }

    if (tlm_resp == TLM_COMPLETED) {
      wait(delay);
      // Release the bus
      arbitration_completed.notify(SC_ZERO_TIME);
    }
  }
}