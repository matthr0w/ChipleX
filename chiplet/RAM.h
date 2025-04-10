#pragma once

#include <systemc>

#include <tlm>
#include <tlm_utils/simple_target_socket.h>

#include <vector>

SC_MODULE(RAM) {
public:
  tlm_utils::simple_target_socket<RAM> socket;

  SC_CTOR(RAM);

private:
  std::vector<uint32_t> mem;

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  tlm_utils::peq_with_get<tlm::tlm_generic_payload> peq;
  void process_transaction();

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload & transaction,
                                     tlm::tlm_phase & phase,
                                     sc_core::sc_time & delay);
};