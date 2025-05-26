#pragma once

#include <systemc>

#include "protocol/ChipletExtension.h"

#include "include/configs.h"

using namespace sc_core;
using namespace tlm;

inline unsigned int
get_available_data_bytes_per_flit(tlm_generic_payload &transaction) {
  static const Config &interconnect_config =
      ConfigRegistry::instance().get("Interconnect");

  static const unsigned int flit_size =
      interconnect_config.get<unsigned int>("interconnect_protocol.flit_size");
  static const unsigned int header_size = interconnect_config.get<unsigned int>(
      "interconnect_protocol.header_size");

  ChipletExtension *ext;
  transaction.get_extension(ext);

  unsigned int size = flit_size;

  // flit header
  size -= header_size;

  // flit metadata
  size -= ext->get_flitext_size_bytes();
  // chiplet metadata
  size -= ext->get_stdext_size_bytes();

  // address
  size -= sizeof(uint32_t);

  return size;
}

inline unsigned int get_required_flit_count(tlm_generic_payload &transaction) {
  unsigned int data_size = transaction.get_data_length();
  unsigned int flit_data_size = get_available_data_bytes_per_flit(transaction);

  return (data_size + flit_data_size - 1) / flit_data_size;
}