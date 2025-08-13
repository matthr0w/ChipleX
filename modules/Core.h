#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/Tracker.h"
#include "common/protocol/ChipletPayload.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Core) {
public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_initiator_socket<Core> socket;
  simple_target_socket<Core> irq_socket;

  Core(sc_module_name name, unsigned int chip_id, unsigned int core_id,
       unsigned int chiplet_ram_size, unsigned int fpga_ram_size,
       sc_time irq_delay);

  std::function<void(Core &, UtilizationTracker *)> thread_fn;
  std::function<void(Core &, UtilizationTracker *, tlm_generic_payload *)>
      interrupt_fn;

  void core_thread();
  void interrupt_thread();

  void send_random(unsigned int delay, double write_prob,
                   unsigned int destination_min, unsigned int destination_max,
                   size_t data_size);
  ChipletPayload *send_request(tlm_command command, int request_id,
                               int destination_id, uint32_t address,
                               bool fixed_address, bool is_volatile,
                               unsigned char *data, unsigned int data_size);

private:
  sc_mutex request_mutex;
  unsigned int request;

  std::deque<tlm_generic_payload *> irq_queue;

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int chip_id;
  const unsigned int core_id;
  const unsigned int chiplet_ram_size;
  const unsigned int fpga_ram_size;
  const sc_time irq_delay;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event transaction_done;
  sc_event irq_event;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_irq(tlm_generic_payload & transaction,
                                    tlm_phase & phase, sc_time & delay);
  tlm_sync_enum nb_transport_bw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);
};