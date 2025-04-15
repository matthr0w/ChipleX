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
  simple_target_socket<Interconnect> socket_A_in{"socket_A_in"};
  simple_target_socket<Interconnect> socket_B_in{"socket_B_in"};
  simple_initiator_socket<Interconnect> socket_A_out{"socket_A_out"};
  simple_initiator_socket<Interconnect> socket_B_out{"socket_B_out"};

  SC_CTOR(Interconnect);

private:
  size_t m_buffer_depth;
  size_t m_width_bits;
  sc_time m_clk_cycle;

  void process_tx_buffer_A();
  void process_rx_buffer_A();
  void process_tx_buffer_B();
  void process_rx_buffer_B();

  std::deque<tlm_generic_payload *> tx_buffer_A;
  std::deque<tlm_generic_payload *> rx_buffer_A;
  std::deque<tlm_generic_payload *> tx_buffer_B;
  std::deque<tlm_generic_payload *> rx_buffer_B;

  // -------------------------------------------------------
  // signals
  // -------------------------------------------------------
  sc_signal<bool> ready_A;
  sc_signal<bool> ready_B;
  sc_signal<bool> rx_buffer_A_full;
  sc_signal<bool> rx_buffer_B_full;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_core::sc_event transaction_A_done;
  sc_core::sc_event transaction_B_done;
  sc_event tx_buffer_A_event;
  sc_event rx_buffer_A_event;
  sc_event tx_buffer_B_event;
  sc_event rx_buffer_B_event;

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm::tlm_generic_payload> peq_A_to_B;
  peq_with_get<tlm::tlm_generic_payload> peq_B_to_A;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_A(tlm_generic_payload & transaction,
                                  tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_fw_B(tlm_generic_payload & transaction,
                                  tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw_A(tlm_generic_payload & transaction,
                                  tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw_B(tlm_generic_payload & transaction,
                                  tlm_phase & phase, sc_time & delay);

  // helper functions
  sc_time compute_delay(tlm_generic_payload * transaction);

  tlm_generic_payload *copy_transaction(tlm_generic_payload & transaction) {
    tlm_generic_payload *copy = new tlm_generic_payload;
    copy->deep_copy_from(transaction);

    if (transaction.get_data_ptr() && transaction.get_data_length() > 0) {
      unsigned char *copy_data =
          new unsigned char[transaction.get_data_length()];
      std::memcpy(copy_data, transaction.get_data_ptr(),
                  transaction.get_data_length());
      copy->set_data_ptr(copy_data);
    }

    return copy;
  }

  void free_transaction(tlm_generic_payload * transaction) {
    if (transaction) {
      if (transaction->get_data_ptr()) {
        delete[] transaction->get_data_ptr();
      }

      delete transaction;
    }
  }
};