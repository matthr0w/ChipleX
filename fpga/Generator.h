#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/Tracker.h"
#include "common/protocol/ChipletPayload.h"

#include "include/configs.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

namespace fpga {
SC_MODULE(Generator) {
public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_initiator_socket<Generator> socket;
  simple_target_socket<Generator> irq_socket;

  Generator(sc_module_name name, unsigned int fpga_id);

  std::function<void(Generator &, UtilizationTracker *)> gen_fn;
  std::function<void(Generator &, UtilizationTracker *, tlm_generic_payload *)>
      interrupt_fn;

  void gen_thread();
  void interrupt_thread();

  void send_random(unsigned int delay, double write_prob,
                   unsigned int destination_min, unsigned int destination_max,
                   size_t data_size);
  ChipletPayload *send_request(tlm_command command, int request_id,
                               int destination_id, uint32_t address,
                               bool fixed_address, bool is_volatile,
                               unsigned char *data, unsigned int data_size);

private:
  const unsigned int fpga_id;

  sc_mutex request_mutex;
  unsigned int request;

  std::deque<tlm_generic_payload *> irq_queue;

  // -------------------------------------------------------
  // config
  // -------------------------------------------------------
  const Config &chiplet_config = ConfigRegistry::instance().get("Chiplet");
  const Config &fpga_config = ConfigRegistry::instance().get("FPGA");
  const Config &interconnect_config =
      ConfigRegistry::instance().get("Interconnect");

  const unsigned int chiplet_ram_size =
      chiplet_config.get<unsigned int>("ram.size");
  const unsigned int fpga_ram_size = fpga_config.get<unsigned int>("ram.size");
  const sc_time irq_delay =
      interconnect_config.get<sc_time>("interconnect_protocol.irq_delay");

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
}; // namespace fpga