#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "configs.h"
#include "globals.h"
#include "logging.h"

#include "modules/interconnects/serial_link/Types.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(SLChannelAllocater) {
public:
  // -------------------------------------------------------
  // Sockets
  // -------------------------------------------------------
  simple_target_socket_tagged<SLChannelAllocater>
      data_in_tsocket; // tagged for InterconnectBase
  simple_initiator_socket<SLChannelAllocater> data_in_isocket;
  simple_target_socket<SLChannelAllocater> data_out_tsocket;
  simple_initiator_socket_tagged<SLChannelAllocater>
      data_out_isocket; // tagged for InterconnectBase

  SLChannelAllocater(sc_module_name name, unsigned axi_width, double distance);

private:
  // -------------------------------------------------------
  // Parameters
  // -------------------------------------------------------
  const unsigned axi_width;
  const double distance;

  // -------------------------------------------------------
  // Transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_data_in(int id,
                                        tlm_generic_payload &transaction,
                                        tlm_phase &phase, sc_time &delay);
  tlm_sync_enum nb_transport_bw_data_in(tlm_generic_payload & transaction,
                                        tlm_phase & phase, sc_time & delay);
  tlm_sync_enum nb_transport_fw_data_out(tlm_generic_payload & transaction,
                                         tlm_phase & phase, sc_time & delay);
  tlm_sync_enum nb_transport_bw_data_out(int id,
                                         tlm_generic_payload &transaction,
                                         tlm_phase &phase, sc_time &delay);

  // -------------------------------------------------------
  // Delay model
  // -------------------------------------------------------
  struct DelayModel {
  private:
    const SLChannelAllocater &module;

  public:
    DelayModel(const SLChannelAllocater &m) : module(m) {}

    sc_time transfer_delay(tlm_generic_payload &transaction) const {
      static const Config &interconnect_config =
          ConfigRegistry::instance().get("Interconnect");

      sc_time delay = SC_ZERO_TIME;
      sc_time packet_transfer_delay = SC_ZERO_TIME;
      sc_time wire_propagation_delay = SC_ZERO_TIME;

      unsigned bandwidth = interconnect_config.get<unsigned>("num_channels") *
                           interconnect_config.get<unsigned>("num_lanes") *
                           (interconnect_config.get<bool>("ddr") ? 2 : 1);

      unsigned num_cycles =
          (Payload_t::simulation_size(module.axi_width) * 8 + bandwidth - 1) /
          bandwidth;

      packet_transfer_delay =
          num_cycles * interconnect_config.get<sc_time>("clk_cycle");

      // Wire propagation delay based on distance
      wire_propagation_delay = sc_time(module.distance * wire_ps_per_mm, SC_PS);

      delay = packet_transfer_delay + wire_propagation_delay;

      SC_LOG_DELAY(&module, "Die to Die Transfer", delay);
      return delay;
    }
  };

  DelayModel delays{*this};
};