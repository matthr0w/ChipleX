#pragma once

#include <systemc>

#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

SC_MODULE(Bus) {
public:
  tlm_utils::simple_target_socket<Bus> target_socket_core1;
  tlm_utils::simple_target_socket<Bus> target_socket_core2;
  tlm_utils::simple_initiator_socket<Bus> initiator_socket;

  SC_CTOR(Bus);

  void set_address_range(uint32_t base, uint32_t size);
  void set_bus_frequency(double mhz);

private:
  enum BusOwner { NONE, CORE1, CORE2 };

  BusOwner current_owner;
  sc_core::sc_time arbitration_delay;
  sc_core::sc_event arbitration_completed;

  uint32_t base_addr;
  uint32_t addr_size;
  sc_core::sc_time cycle;

  tlm_utils::peq_with_get<tlm::tlm_generic_payload> peq1;
  tlm_utils::peq_with_get<tlm::tlm_generic_payload> peq2;

  tlm::tlm_sync_enum nb_transport_fw_core1(
      tlm::tlm_generic_payload &, tlm::tlm_phase &, sc_core::sc_time &);
  tlm::tlm_sync_enum nb_transport_fw_core2(
      tlm::tlm_generic_payload &, tlm::tlm_phase &, sc_core::sc_time &);
  tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload &,
                                     tlm::tlm_phase &, sc_core::sc_time &);

  void serve_core1();
  void serve_core2();
};