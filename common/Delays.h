#pragma once

#include <systemc>

#include "Tracker.h"
#include "protocol/ChipletExtension.h"
#include "protocol/ChipletPayload.h"

#include "include/configs.h"
#include "include/globals.h"
#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

// -------------------------------------------------------
// helper functions
// -------------------------------------------------------
inline sc_time get_data_cycles_delay(tlm_generic_payload &transaction,
                                     unsigned int width, sc_time cycle) {
  unsigned int data_size = transaction.get_data_length();
  unsigned int num_cycles = (data_size * 8 + width - 1) / width;
  return num_cycles * cycle;
}

inline sc_time get_extension_cycles_delay(tlm_generic_payload &transaction,
                                          unsigned int width, sc_time cycle) {
  ChipletExtension *ext;
  transaction.get_extension(ext);

  unsigned int flitext_size = 0;

  if (ext->flit_id != -1) {
    flitext_size = ext->get_flitext_size_bytes();
  }

  unsigned int stdext_size = ext->get_stdext_size_bytes();
  unsigned int num_cycles =
      ((stdext_size + flitext_size) * 8 + width - 1) / width;
  return num_cycles * cycle;
}

inline sc_time get_bandwidth_transfer_delay(unsigned int size_bytes,
                                            double bandwidth) {
  return sc_time(static_cast<double>(size_bytes) * 8.0 / bandwidth, SC_NS);
}

// -------------------------------------------------------
// Bus
// -------------------------------------------------------
inline sc_time get_bus_arbitration_delay(sc_module &module,
                                         tlm_generic_payload &transaction,
                                         sc_time arbitration_delay) {
  // Bus Arbitration Delay
  // -----------------------------------------------
  //      + fixed arbitration delay

  sc_time delay = arbitration_delay;

  SC_LOG_DELAY(&module, transaction, "Bus Arbitration", delay);
  return delay;
}

inline sc_time get_bus_transfer_fw_delay(sc_module &module,
                                         tlm_generic_payload &transaction,
                                         sc_time clk_cycle,
                                         unsigned int width) {
  // Bus Transfer Forward Delay
  // -----------------------------------------------
  // read operation:
  //      + address cycle delay
  //      - no data cycles delay
  //      + extension cycles delay
  // write operation:
  //      + address cycle delay
  //      + data cycles delay
  //      + extension cycles delay

  sc_time delay = SC_ZERO_TIME;
  sc_time address_cycle_delay = SC_ZERO_TIME;
  sc_time data_cycles_delay = SC_ZERO_TIME;
  sc_time extension_cycles_delay = SC_ZERO_TIME;

  if (transaction.get_command() == TLM_READ_COMMAND) {
    address_cycle_delay = clk_cycle;
    data_cycles_delay = SC_ZERO_TIME;
    extension_cycles_delay =
        get_extension_cycles_delay(transaction, width, clk_cycle);
  } else if (transaction.get_command() == TLM_WRITE_COMMAND) {
    address_cycle_delay = clk_cycle;
    data_cycles_delay = get_data_cycles_delay(transaction, width, clk_cycle);
    extension_cycles_delay =
        get_extension_cycles_delay(transaction, width, clk_cycle);
  }

  delay = address_cycle_delay + data_cycles_delay + extension_cycles_delay;

  SC_LOG_DELAY(&module, transaction, "Bus Transfer Forward", delay);
  return delay;
}

inline sc_time get_bus_transfer_bw_delay(sc_module &module,
                                         tlm_generic_payload &transaction,
                                         sc_time clk_cycle,
                                         unsigned int width) {
  // Bus Transfer Backward Delay
  // -----------------------------------------------
  // read operation:
  //      - no address cycle delay
  //      + data cycles delay
  //      + extension cycles delay
  // write operation:
  //      - no address cycle delay
  //      - no data cycles delay
  //      - extension cycles delay

  sc_time delay = SC_ZERO_TIME;
  sc_time address_cycle_delay = SC_ZERO_TIME;
  sc_time data_cycles_delay = SC_ZERO_TIME;
  sc_time extension_cycles_delay = SC_ZERO_TIME;

  if (transaction.get_command() == TLM_READ_COMMAND) {
    address_cycle_delay = SC_ZERO_TIME;
    data_cycles_delay = get_data_cycles_delay(transaction, width, clk_cycle);
    extension_cycles_delay =
        get_extension_cycles_delay(transaction, width, clk_cycle);
  } else if (transaction.get_command() == TLM_WRITE_COMMAND) {
    address_cycle_delay = SC_ZERO_TIME;
    data_cycles_delay = SC_ZERO_TIME;
    extension_cycles_delay = SC_ZERO_TIME;
  }

  delay = address_cycle_delay + data_cycles_delay + extension_cycles_delay;

  SC_LOG_DELAY(&module, transaction, "Bus Transfer Backward", delay);
  return delay;
}

// -------------------------------------------------------
// Cache
// -------------------------------------------------------
inline sc_time get_cache_arbitration_delay(sc_module &module,
                                           tlm_generic_payload &transaction,
                                           sc_time arbitration_delay) {
  // Cache Arbitration Delay
  // -----------------------------------------------
  //      + fixed arbitration delay

  sc_time delay = arbitration_delay;

  SC_LOG_DELAY(&module, transaction, "Cache Arbitration", delay);
  return delay;
}

inline sc_time get_cache_access_delay(sc_module &module,
                                      tlm_generic_payload &transaction,
                                      sc_time access_delay) {
  // Cache Access Delay
  // -----------------------------------------------
  //      + fixed access delay

  sc_time delay = SC_ZERO_TIME;

  delay = access_delay;

  SC_LOG_DELAY(&module, transaction, "Cache Access", delay);
  return delay;
}

// -------------------------------------------------------
// RAM
// -------------------------------------------------------
inline sc_time
get_mem_address_assignment_delay(sc_module &module,
                                 tlm_generic_payload &transaction,
                                 sc_time assignment_delay) {
  // Memory Controller Address Assignment Delay
  // -----------------------------------------------
  //      + fixed assignment delay

  sc_time delay = SC_ZERO_TIME;

  delay = assignment_delay;

  SC_LOG_DELAY(&module, transaction, "Memory Controller Address Assignment",
               delay);
  return delay;
}

inline sc_time get_mem_access_delay(sc_module &module,
                                    tlm_generic_payload &transaction,
                                    sc_time clk_cycle, sc_time access_delay,
                                    unsigned int width) {
  // RAM Access Delay
  // -----------------------------------------------
  //      + fixed access delay
  //      + data cycles delay

  sc_time delay = SC_ZERO_TIME;
  sc_time data_cycles_delay = SC_ZERO_TIME;

  data_cycles_delay = get_data_cycles_delay(transaction, width, clk_cycle);

  delay = access_delay + data_cycles_delay;

  SC_LOG_DELAY(&module, transaction, "RAM Access", delay);
  return delay;
}

// -------------------------------------------------------
// Interconnect Protocol Layer
// -------------------------------------------------------
inline sc_time
get_protocol2interconnect_process_delay(sc_module &module,
                                        tlm_generic_payload &transaction,
                                        sc_time preprocess_delay) {
  // Protocol Layer to Interconnect Process Delay
  // -----------------------------------------------
  //      + fixed preprocess delay

  sc_time delay = SC_ZERO_TIME;

  delay = preprocess_delay;

  SC_LOG_DELAY(&module, transaction, "Protocol Layer to Interconnect Process",
               delay);
  return delay;
}

inline sc_time
get_interconnect2protocol_process_delay(sc_module &module,
                                        tlm_generic_payload &transaction,
                                        sc_time postprocess_delay) {
  // Interconnect to Protocol Layer Process Delay
  // -----------------------------------------------
  //      + fixed postprocess delay

  sc_time delay = SC_ZERO_TIME;

  delay = postprocess_delay;

  SC_LOG_DELAY(&module, transaction, "Interconnect to Protocol Layer Process",
               delay);
  return delay;
}

inline sc_time get_irq_transfer_delay(sc_module &module,
                                      tlm_generic_payload &transaction,
                                      sc_time irq_delay) {
  // IRQ Transfer Delay
  // -----------------------------------------------
  //      + fixed irq delay

  sc_time delay = SC_ZERO_TIME;

  delay = irq_delay;

  SC_LOG_DELAY(&module, transaction, "IRQ Transfer", delay);
  return delay;
}

// -------------------------------------------------------
// Interconnect Physical Layer
// -------------------------------------------------------
inline sc_time get_die2die_transfer_delay(sc_module &module,
                                          tlm_generic_payload &transaction,
                                          double bandwidth, double distance,
                                          unsigned int flit_size) {
  // Die to Die Transfer Delay
  // -----------------------------------------------
  //      + flit transfer delay
  //      + wire propagation delay

  static const Config &interconnect_config =
      ConfigRegistry::instance().get("Interconnect");

  sc_time delay = SC_ZERO_TIME;
  sc_time flit_transfer_delay = SC_ZERO_TIME;
  sc_time wire_propagation_delay = SC_ZERO_TIME;

  flit_transfer_delay = get_bandwidth_transfer_delay(flit_size, bandwidth);

  // wire propagation delay based on distance
  wire_propagation_delay = sc_time(distance * wire_ps_per_mm, SC_PS);

  delay = flit_transfer_delay + wire_propagation_delay;
  sc_time base_transfer_delay = delay;

  double prob_bad_transfer =
      1.0 - std::pow(1.0 - bit_error_rate, flit_size * 8);

  int max_attempts = 1;
  switch (connection_type) {
  case ConnectionType::UCIe:
    max_attempts =
        interconnect_config.get<unsigned int>("interconnect_protocol.retries");
    break;
  default:
    break;
  }

  bool transfer_successful = false;

  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    TransmissionTracker::instance().record_transmission(base_transfer_delay);

    if (bit_error_dist(bit_error_gen) >= prob_bad_transfer) {
      // no bit error
      transfer_successful = true;
      break;
    }

    SC_LOG_ERROR(&module, transaction,
                 "Bit error on attempt " + std::to_string(attempt + 1));

    switch (connection_type) {
    case ConnectionType::PCIe:
      // forward error correction penalty
      delay +=
          interconnect_config.get<sc_time>("interconnect_protocol.fec_delay");
      TransmissionTracker::instance().record_attempt();
      // assuming FEC handles it
      transfer_successful = true;
      break;
    case ConnectionType::UCIe:
      // retry penalty
      delay += base_transfer_delay;
      TransmissionTracker::instance().record_attempt();
    default:;
    }
  }

  static_cast<ChipletPayload *>(&transaction)
      ->set_transfer_result(transfer_successful);

  if (!transfer_successful) {
    SC_LOG_ERROR(&module, transaction, "Transfer failed");
  }

  SC_LOG_DELAY(&module, transaction, "Die to Die Transfer", delay);
  return delay;
}