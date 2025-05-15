#pragma once

#include "systemc"

#include "protocol/ChipletExtension.h"

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;

// helper functions
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
// delays
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
                                         sc_time clc_cycle,
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

  sc_time delay;
  sc_time address_cycle_delay;
  sc_time data_cycles_delay;
  sc_time extension_cycles_delay;

  if (transaction.get_command() == TLM_READ_COMMAND) {
    address_cycle_delay = clc_cycle;
    data_cycles_delay = SC_ZERO_TIME;
    extension_cycles_delay =
        get_extension_cycles_delay(transaction, width, clc_cycle);
  } else if (transaction.get_command() == TLM_WRITE_COMMAND) {
    address_cycle_delay = clc_cycle;
    data_cycles_delay = get_data_cycles_delay(transaction, width, clc_cycle);
    extension_cycles_delay =
        get_extension_cycles_delay(transaction, width, clc_cycle);
  }

  delay = address_cycle_delay + data_cycles_delay + extension_cycles_delay;

  SC_LOG_DELAY(&module, transaction, "Bus Transfer Forward", delay);
  return delay;
}

inline sc_time get_bus_transfer_bw_delay(sc_module &module,
                                         tlm_generic_payload &transaction,
                                         sc_time clc_cycle,
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

  sc_time delay;
  sc_time address_cycle_delay;
  sc_time data_cycles_delay;
  sc_time extension_cycles_delay;

  if (transaction.get_command() == TLM_READ_COMMAND) {
    address_cycle_delay = SC_ZERO_TIME;
    data_cycles_delay = get_data_cycles_delay(transaction, width, clc_cycle);
    extension_cycles_delay =
        get_extension_cycles_delay(transaction, width, clc_cycle);
  } else if (transaction.get_command() == TLM_WRITE_COMMAND) {
    address_cycle_delay = SC_ZERO_TIME;
    data_cycles_delay = SC_ZERO_TIME;
    extension_cycles_delay =
        get_extension_cycles_delay(transaction, width, clc_cycle);
  }

  delay = address_cycle_delay + data_cycles_delay + extension_cycles_delay;

  SC_LOG_DELAY(&module, transaction, "Bus Transfer Backward", delay);
  return delay;
}

inline sc_time get_mem_access_delay(sc_module &module,
                                    tlm_generic_payload &transaction,
                                    sc_time clc_cycle, sc_time access_delay,
                                    unsigned int width) {
  // RAM Access Delay
  // -----------------------------------------------
  //      + fixed access delay
  //      + data cycles delay

  sc_time delay;
  sc_time data_cycles_delay;

  data_cycles_delay = get_data_cycles_delay(transaction, width, clc_cycle);

  delay = access_delay + data_cycles_delay;

  SC_LOG_DELAY(&module, transaction, "RAM Access", delay);
  return delay;
}

inline sc_time get_protocol2interconnect_transfer_delay(
    sc_module &module, tlm_generic_payload &transaction, sc_time clc_cycle,
    unsigned int width) {
  // Protocol Layer to Interconnect Transfer Delay
  // Interconnect to Protocol Layer Transfer Delay
  // -----------------------------------------------
  // read operation pending:
  //      + address cycle delay
  //      - no data cycles delay
  //      + extension cycles delay
  // read operation done:
  //      - no address cycle delay
  //      + data cycles delay
  //      + extension cycles delay
  // write operation pending:
  //      + address cycle delay
  //      + data cycles delay
  //      + extension cycles delay
  // write operation done:
  //      - no backward call

  sc_time delay;
  sc_time address_cycle_delay;
  sc_time data_cycles_delay;
  sc_time extension_cycles_delay;

  if (transaction.get_command() == TLM_READ_COMMAND) {
    // destination_id != source_id -> read operation pending
    // (see bus implementation)
    ChipletExtension *ext;
    transaction.get_extension(ext);
    if (ext->destination_id != ext->source_id) {
      address_cycle_delay = clc_cycle;
      data_cycles_delay = SC_ZERO_TIME;
      extension_cycles_delay =
          get_extension_cycles_delay(transaction, width, clc_cycle);
    } else {
      address_cycle_delay = SC_ZERO_TIME;
      data_cycles_delay = get_data_cycles_delay(transaction, width, clc_cycle);
      extension_cycles_delay =
          get_extension_cycles_delay(transaction, width, clc_cycle);
    }
  } else if (transaction.get_command() == TLM_WRITE_COMMAND) {
    address_cycle_delay = clc_cycle;
    data_cycles_delay = get_data_cycles_delay(transaction, width, clc_cycle);
    extension_cycles_delay =
        get_extension_cycles_delay(transaction, width, clc_cycle);
  }

  delay = address_cycle_delay + data_cycles_delay + extension_cycles_delay;

  SC_LOG_DELAY(&module, transaction, "Protocol Layer to Interconnect Transfer",
               delay);
  return delay;
}

inline sc_time
get_chiplet2chiplet_transfer_delay(sc_module &module,
                                   tlm_generic_payload &transaction,
                                   sc_time clc_cycle, unsigned int width) {
  // Chiplet to Chiplet Transfer Delay
  // -----------------------------------------------
  // read operation pending:
  //      + address cycle delay
  //      - no data cycles delay
  //      + extension cycles delay
  // read operation done:
  //      - no address cycle delay
  //      + data cycles delay
  //      + extension cycles delay
  // write operation pending:
  //      + address cycle delay
  //      + data cycles delay
  //      + extension cycles delay
  // write operation done:
  //      - no backward call

  sc_time delay;
  sc_time address_cycle_delay;
  sc_time data_cycles_delay;
  sc_time extension_cycles_delay;

  if (transaction.get_command() == TLM_READ_COMMAND) {
    // destination_id != source_id -> read operation pending
    // (see bus implementation)
    ChipletExtension *ext;
    transaction.get_extension(ext);
    if (ext->destination_id != ext->source_id) {
      address_cycle_delay = clc_cycle;
      data_cycles_delay = SC_ZERO_TIME;
      extension_cycles_delay =
          get_extension_cycles_delay(transaction, width, clc_cycle);
    } else {
      address_cycle_delay = SC_ZERO_TIME;
      data_cycles_delay = get_data_cycles_delay(transaction, width, clc_cycle);
      extension_cycles_delay =
          get_extension_cycles_delay(transaction, width, clc_cycle);
    }
  } else if (transaction.get_command() == TLM_WRITE_COMMAND) {
    address_cycle_delay = clc_cycle;
    data_cycles_delay = get_data_cycles_delay(transaction, width, clc_cycle);
    extension_cycles_delay =
        get_extension_cycles_delay(transaction, width, clc_cycle);
  }

  delay = address_cycle_delay + data_cycles_delay + extension_cycles_delay;

  SC_LOG_DELAY(&module, transaction, "Chiplet to Chiplet Transfer", delay);
  return delay;
}

inline sc_time get_fpga2chiplet_transfer_delay(sc_module &module,
                                               tlm_generic_payload &transaction,
                                               sc_time clc_cycle,
                                               unsigned int width) {
  // FPGA to Chiplet Transfer Delay
  // -----------------------------------------------
  // read operation pending:
  //      + address cycle delay
  //      - no data cycles delay
  //      + extension cycles delay
  // read operation done:
  //      - no address cycle delay
  //      + data cycles delay
  //      + extension cycles delay
  // write operation pending:
  //      + address cycle delay
  //      + data cycles delay
  //      + extension cycles delay
  // write operation done:
  //      - no backward call

  sc_time delay;
  sc_time address_cycle_delay;
  sc_time data_cycles_delay;
  sc_time extension_cycles_delay;

  if (transaction.get_command() == TLM_READ_COMMAND) {
    // destination_id != source_id -> read operation pending
    // (see bus implementation)
    ChipletExtension *ext;
    transaction.get_extension(ext);
    if (ext->destination_id != ext->source_id) {
      address_cycle_delay = clc_cycle;
      data_cycles_delay = SC_ZERO_TIME;
      extension_cycles_delay =
          get_extension_cycles_delay(transaction, width, clc_cycle);
    } else {
      address_cycle_delay = SC_ZERO_TIME;
      data_cycles_delay = get_data_cycles_delay(transaction, width, clc_cycle);
      extension_cycles_delay =
          get_extension_cycles_delay(transaction, width, clc_cycle);
    }
  } else if (transaction.get_command() == TLM_WRITE_COMMAND) {
    address_cycle_delay = clc_cycle;
    data_cycles_delay = get_data_cycles_delay(transaction, width, clc_cycle);
    extension_cycles_delay =
        get_extension_cycles_delay(transaction, width, clc_cycle);
  }

  delay = address_cycle_delay + data_cycles_delay + extension_cycles_delay;

  SC_LOG_DELAY(&module, transaction, "FPGA to Chiplet Transfer", delay);
  return delay;
}

inline sc_time get_irq_transfer_delay(sc_module &module,
                                      tlm_generic_payload &transaction,
                                      sc_time clc_cycle) {
  // IRQ Transfer Delay
  // -----------------------------------------------
  //      + address cycle delay
  //      + irq cycle delay

  sc_time delay;
  sc_time address_cycle_delay = clc_cycle;
  sc_time irq_cycle_delay = clc_cycle;

  delay = address_cycle_delay + irq_cycle_delay;

  SC_LOG_DELAY(&module, transaction, "IRQ Transfer", delay);
  return delay;
}