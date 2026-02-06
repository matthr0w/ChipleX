#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "globals.h"
#include "logging.h"

#include "modules/interconnects/serial_link/Types.h"
#include "setup/Types.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(SLChannelAllocater) {
private:
  // -------------------------------------------------------
  // Config
  // -------------------------------------------------------
  const unsigned link_id;
  const std::vector<ConnectionConfig> connections;
  // InterconnectBase
  const unsigned axi_width;

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

  SLChannelAllocater(sc_module_name name, unsigned link_id,
                     InterconnectConfig interconnect_config, unsigned num_links,
                     unsigned num_cores, unsigned axi_width);

private:
  // -------------------------------------------------------
  // Delay Model
  // -------------------------------------------------------
  struct DelayModel {
  private:
    const SLChannelAllocater &module;

  public:
    DelayModel(const SLChannelAllocater &m) : module(m) {}

    sc_time transfer_delay(int id, tlm_generic_payload &transaction) const {
      ConnectionConfig connection = module.connections[id];
      YAML::Node config = connection.node;

      sc_time delay = SC_ZERO_TIME;
      sc_time packet_transfer_delay = SC_ZERO_TIME;
      sc_time wire_propagation_delay = SC_ZERO_TIME;

      sc_time clk_cycle(config["clk_cycle"].as<unsigned>(), SC_NS);
      bool ddr = config["ddr"].as<bool>();
      unsigned num_channels = config["num_channels"].as<unsigned>();
      unsigned num_lanes = config["num_lanes"].as<unsigned>();

      unsigned bandwidth = num_channels * num_lanes * (ddr ? 2 : 1);

      unsigned payload_bits = Payload_t::simulation_size(module.axi_width) * 8;
      unsigned num_cycles = (payload_bits + bandwidth - 1) / bandwidth;

      packet_transfer_delay = num_cycles * clk_cycle;

      // Wire propagation delay based on wire length
      wire_propagation_delay =
          sc_time(connection.wire_length * wire_ps_per_mm, SC_PS);

      delay = packet_transfer_delay + wire_propagation_delay;

      // Bit error simulation
      double scaled_ber =
          std::clamp(bit_error_rate * connection.ber_scalar, 0.0, 1.0);
      double prob_bad_transfer = 1.0 - std::pow(1.0 - scaled_ber, payload_bits);
      bool transfer_successful =
          (bit_error_dist(bit_error_gen) >= prob_bad_transfer);

      if (!transfer_successful) {
        SC_LOG_WARN(&module, "Transmission error detected: flit corrupted and "
                             "payload invalidated.");
        unsigned char *data = transaction.get_data_ptr();
        // Drop flit by zeroing AXI beat bytes
        *reinterpret_cast<uint32_t *>(data + AXI_ADDR_WIRE_OFFSET) = 0;
        std::memset(data + AXI_DATA_WIRE_OFFSET, 0, (module.axi_width + 7) / 8);
      }

      SC_LOG_DELAY(&module, "Die to Die Transfer", delay);
      return delay;
    }
  };

  DelayModel delays{*this};

  // -------------------------------------------------------
  // Transport Functions
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
};