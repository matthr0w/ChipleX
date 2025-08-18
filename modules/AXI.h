#pragma once

#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(AXI) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  multi_passthrough_target_socket<AXI> target_socket;
  simple_initiator_socket<AXI> initiator_socket;

  AXI(sc_module_name name, unsigned int read_channel_width,
      unsigned int write_channel_width, sc_time read_channel_clk_cycle,
      sc_time write_channel_clk_cycle);

private:
  void process_read_channel();
  void process_write_channel();

  struct Request {
    int module;
    tlm_generic_payload *transaction;
    tlm_phase *phase;
    sc_time *delay;
  };

  sc_mutex request_mutex;
  std::unordered_map<tlm::tlm_generic_payload *, int> requests_map;

  std::deque<Request> read_channel;
  std::deque<Request> write_channel;
  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int read_channel_width;
  const unsigned int write_channel_width;
  const sc_time read_channel_clk_cycle;
  const sc_time write_channel_clk_cycle;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event read_request_issued;
  sc_event write_request_issued;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(int id, tlm_generic_payload &transaction,
                                tlm_phase &phase, sc_time &delay);

  tlm_sync_enum nb_transport_bw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);
};