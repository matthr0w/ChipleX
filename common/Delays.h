#pragma once

#include <systemc>

#include "Flits.h"
#include "protocol/ChipletExtension.h"

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
  unsigned int num_cycles = (data_size + width - 1) / width;
  return num_cycles * cycle;
}

inline sc_time get_extension_cycles_delay(tlm_generic_payload &transaction,
                                          unsigned int width, sc_time cycle) {
  ChipletExtension *ext;
  transaction.get_extension(ext);
  unsigned int extension_size = ext->get_size_bytes();
  unsigned int num_cycles = (extension_size + width - 1) / width;
  return num_cycles * cycle;
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
  //      + extension cycles delay

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
    extension_cycles_delay =
        get_extension_cycles_delay(transaction, width, clk_cycle);
  }

  delay = address_cycle_delay + data_cycles_delay + extension_cycles_delay;

  SC_LOG_DELAY(&module, transaction, "Bus Transfer Backward", delay);
  return delay;
}

// -------------------------------------------------------
// RAM
// -------------------------------------------------------
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
inline sc_time get_protocol2interconnect_transfer_delay(
    sc_module &module, tlm_generic_payload &transaction, sc_time clk_cycle,
    sc_time preprocess_delay, unsigned int width, unsigned int flit_size,
    unsigned int header_size) {
  // Protocol Layer to Interconnect Transfer Delay
  // -----------------------------------------------
  //      + fixed preprocess delay
  //      + flit cycles delay

  sc_time delay = SC_ZERO_TIME;
  sc_time flit_cycles_delay = SC_ZERO_TIME;

  unsigned int transaction_flit_size =
      get_flit_bytes(transaction, flit_size, header_size);

  unsigned int num_cycles = (transaction_flit_size + width - 1) / width;

  flit_cycles_delay = num_cycles * clk_cycle;

  delay = preprocess_delay + flit_cycles_delay;

  SC_LOG_DELAY(&module, transaction, "Protocol Layer to Interconnect Transfer",
               delay);
  return delay;
}

inline sc_time get_interconnect2protocol_transfer_delay(
    sc_module &module, tlm_generic_payload &transaction, sc_time clk_cycle,
    unsigned int width, unsigned int flit_size, unsigned int header_size) {
  // Interconnect to Protocol Layer Transfer Delay
  // -----------------------------------------------
  //      - no extra delay (already in chiplet2chiplet)
  //      + flit cycles delay

  sc_time delay = SC_ZERO_TIME;
  sc_time flit_cycles_delay = SC_ZERO_TIME;

  unsigned int transaction_flit_size =
      get_flit_bytes(transaction, flit_size, header_size);

  unsigned int num_cycles = (transaction_flit_size + width - 1) / width;

  flit_cycles_delay = num_cycles * clk_cycle;

  delay = flit_cycles_delay;

  SC_LOG_DELAY(&module, transaction, "Interconnect to Protocol Layer Transfer",
               delay);
  return delay;
}

inline sc_time get_irq_transfer_delay(sc_module &module,
                                      tlm_generic_payload &transaction,
                                      sc_time clk_cycle) {
  // IRQ Transfer Delay
  // -----------------------------------------------
  //      + address cycle delay
  //      + irq cycle delay

  sc_time delay;
  sc_time address_cycle_delay = clk_cycle;
  sc_time irq_cycle_delay = clk_cycle;

  delay = address_cycle_delay + irq_cycle_delay;

  SC_LOG_DELAY(&module, transaction, "IRQ Transfer", delay);
  return delay;
}

// -------------------------------------------------------
// Interconnect Physical Layer
// -------------------------------------------------------
inline sc_time get_chiplet2chiplet_transfer_delay(
    sc_module &module, tlm_generic_payload &transaction, sc_time clk_cycle,
    sc_time postprocess_delay, unsigned int width, unsigned int flit_size,
    unsigned int header_size) {
  // Chiplet to Chiplet Transfer Delay
  // -----------------------------------------------
  //      + flit cycles delay
  //      + wire propagation delay
  //      + fixed postprocess delay

  sc_time delay = SC_ZERO_TIME;
  sc_time flit_cycles_delay = SC_ZERO_TIME;
  sc_time wire_propagation_delay = SC_ZERO_TIME;

  unsigned int transaction_flit_size =
      get_flit_bytes(transaction, flit_size, header_size);

  unsigned int num_cycles = (transaction_flit_size + width - 1) / width;

  flit_cycles_delay = num_cycles * clk_cycle;

  // wire propagation delay based on distance
  wire_propagation_delay =
      sc_time(chiplet_distance_um * wire_ps_per_mm / 1000, SC_PS);

  delay = flit_cycles_delay + wire_propagation_delay + postprocess_delay;

  bool bad_transfer = false;
  double prob_bad_transfer =
      1.0 - std::pow(1.0 - bit_error_rate, transaction_flit_size * 8);

  if (bit_error_dist(bit_error_gen) < prob_bad_transfer) {
    SC_LOG_ERROR(&module, transaction, "Bit error");
    bad_transfer = true;
  }

  // connection type specific delays
  switch (connection_type) {
  case ConnectionType::UCIe: {
    // UCIe retry mechanism
    // if bit error happens -> retry transfer -> double delay
    if (bad_transfer) {
      delay *= 2;
    }

    break;
  }
  default:
    break;
  }

  SC_LOG_DELAY(&module, transaction, "Chiplet to Chiplet Transfer", delay);
  return delay;
}

inline sc_time get_fpga2chiplet_transfer_delay(
    sc_module &module, tlm_generic_payload &transaction, sc_time clk_cycle,
    sc_time postprocess_delay, unsigned int width, unsigned int flit_size,
    unsigned int header_size) {
  // FPGA to Chiplet Transfer Delay
  // -----------------------------------------------
  //      + flit cycles delay
  //      + wire propagation delay
  //      + fixed postprocess delay

  sc_time delay = SC_ZERO_TIME;
  sc_time flit_cycles_delay = SC_ZERO_TIME;
  sc_time wire_propagation_delay = SC_ZERO_TIME;

  unsigned int transaction_flit_size =
      get_flit_bytes(transaction, flit_size, header_size);

  unsigned int num_cycles = (transaction_flit_size + width - 1) / width;

  flit_cycles_delay = num_cycles * clk_cycle;

  // wire propagation delay based on distance
  wire_propagation_delay = sc_time(fpga_distance_mm * wire_ps_per_mm, SC_PS);

  delay = flit_cycles_delay + wire_propagation_delay + postprocess_delay;

  bool bad_transfer = false;
  double prob_bad_transfer =
      1.0 - std::pow(1.0 - bit_error_rate, transaction_flit_size * 8);

  if (bit_error_dist(bit_error_gen) < prob_bad_transfer) {
    SC_LOG_ERROR(&module, transaction, "Bit error");
    bad_transfer = true;
  }

  // connection type specific delays
  switch (connection_type) {
  case ConnectionType::UCIe: {
    // UCIe retry mechanism
    // if bit error happens -> retry transfer -> double delay
    if (bad_transfer) {
      delay *= 2;
    }

    break;
  }
  default:
    break;
  }

  SC_LOG_DELAY(&module, transaction, "FPGA to Chiplet Transfer", delay);
  return delay;
}