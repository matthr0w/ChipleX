#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Interconnect) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_target_socket<Interconnect> bus_target_socket;
  simple_initiator_socket<Interconnect> bus_initiator_socket;
  simple_target_socket<Interconnect> interconnect_target_socket;
  simple_initiator_socket<Interconnect> interconnect_initiator_socket;
  simple_initiator_socket<Interconnect> core0_irq_initiator_socket;
  simple_initiator_socket<Interconnect> core1_irq_initiator_socket;

  Interconnect(sc_core::sc_module_name name, unsigned int chiplet_id);

private:
  const unsigned int chiplet_id;

  void process_tx_buffer();
  void process_rx_buffer();

  void send_irq(tlm_generic_payload & transaction);

  std::deque<tlm_generic_payload *> tx_buffer;
  std::deque<tlm_generic_payload *> rx_buffer;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event tx_transaction_done;
  sc_event rx_transaction_done;
  sc_event tx_buffer_in_event;
  sc_event tx_buffer_out_event;
  sc_event rx_buffer_in_event;
  sc_event rx_buffer_out_event;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_bus(tlm_generic_payload & transaction,
                                    tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw_bus(tlm_generic_payload & transaction,
                                    tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_fw_interconnect(
      tlm_generic_payload & transaction, tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw_interconnect(
      tlm_generic_payload & transaction, tlm_phase & phase, sc_time & delay);

  // helper functions
  void output_buffer_levels();
};