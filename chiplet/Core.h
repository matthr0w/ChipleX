#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/protocol/ChipletPayload.h"

#include "include/configs.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

namespace chiplet {
SC_MODULE(Core) {
public:
  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_initiator_socket<Core> socket;
  simple_target_socket<Core> irq_socket;

  Core(sc_module_name name, unsigned int chiplet_id, unsigned int core_id);

  std::function<void(Core &)> thread_fn;
  std::function<void(Core &, tlm_generic_payload *)> interrupt_fn;

  void core_thread();

  void send_random(unsigned int delay, double write_prob,
                   unsigned int destination_min, unsigned int destination_max,
                   size_t data_size);
  ChipletPayload *send_request(tlm_command command, int request_id,
                               int destination_id, uint32_t address,
                               unsigned char *data, unsigned int data_size);

private:
  const unsigned int chiplet_id;
  const unsigned int core_id;

  sc_mutex request_mutex;
  unsigned int request;

  // -------------------------------------------------------
  // config
  // -------------------------------------------------------
  const Config &chiplet_config = ConfigRegistry::instance().get("Chiplet");
  const Config &fpga_config = ConfigRegistry::instance().get("FPGA");
  const Config &interconnect_config =
      ConfigRegistry::instance().get("Interconnect");

  const unsigned int bus_width = chiplet_config.get<unsigned int>("bus.width");
  const sc_time bus_clk_cycle = chiplet_config.get<sc_time>("bus.clk_cycle");
  const unsigned int chiplet_ram_size =
      chiplet_config.get<unsigned int>("ram.size");
  const unsigned int fpga_ram_size = fpga_config.get<unsigned int>("ram.size");
  const sc_time irq_delay =
      interconnect_config.get<sc_time>("interconnect_protocol.irq_delay");

  // -------------------------------------------------------
  // peqs
  // -------------------------------------------------------
  peq_with_get<tlm_generic_payload> irq_peq;
  void handle_interrupt();

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event transaction_done;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_irq(tlm_generic_payload & transaction,
                                    tlm_phase & phase, sc_time & delay);
  tlm_sync_enum nb_transport_bw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);
};
}; // namespace chiplet