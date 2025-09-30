#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/System.h"

#include "modules/interconnects/serial_link/FifoIf.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(SLDataLinkLayer) {
private:
  // -------------------------------------------------------
  // Config
  // -------------------------------------------------------
  const unsigned chiplet_id;
  const unsigned num_links;
  const unsigned axi_width;

public:
  // -------------------------------------------------------
  // Signals
  // -------------------------------------------------------
  sc_in<bool> clk;

  // -------------------------------------------------------
  // Ports
  // -------------------------------------------------------
  sc_port<FifoIf> stream_fifo_in;
  sc_port<FifoIf> stream_fifo_out;

  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  simple_target_socket_tagged<SLDataLinkLayer> *data_in_tsockets;
  simple_initiator_socket_tagged<SLDataLinkLayer> *data_out_isockets;

  SLDataLinkLayer(sc_module_name name, unsigned chiplet_id,
                  ChipletConfig chiplet_config,
                  InterconnectConfig interconnect_config);
  ~SLDataLinkLayer();

private:
  // -------------------------------------------------------
  // Internal Declarations
  // -------------------------------------------------------
  bool data_out_ongoing = false;

  void clk_posedge();

  // -------------------------------------------------------
  // Transport Functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw(int id, tlm_generic_payload &transaction,
                                tlm_phase &phase, sc_time &delay);
  tlm_sync_enum nb_transport_bw(int id, tlm_generic_payload &transaction,
                                tlm_phase &phase, sc_time &delay);

  // -------------------------------------------------------
  // Helper Functions
  // -------------------------------------------------------
  void pack_payload(tlm_generic_payload & transaction, Payload_t & payload);
  Payload_t *unpack_payload(tlm_generic_payload & transaction);
};