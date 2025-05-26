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

inline unsigned get_payload_bytes(tlm_generic_payload &transaction) {
  unsigned size = 0;

  ChipletExtension *ext = nullptr;
  transaction.get_extension(ext);

  // address only in requests
  if (ext && (ext->destination_id != ext->source_id)) {
    size += sizeof(uint32_t);
  }

  // data
  size += transaction.get_data_length();

  // extension
  if (ext) {
    size += ext->get_stdext_size_bytes();
  }

  return size;
}

inline unsigned get_protocol_bytes(tlm_generic_payload &transaction,
                                   unsigned header_size) {
  return header_size;
}

inline unsigned get_flit_count(tlm_generic_payload &transaction,
                               unsigned flit_size, unsigned header_size) {
  unsigned total_bytes = get_protocol_bytes(transaction, header_size) +
                         get_payload_bytes(transaction);
  return (total_bytes + flit_size - 1) / flit_size;
}

inline unsigned get_flit_bytes(tlm_generic_payload &transaction,
                               unsigned flit_size, unsigned header_size) {
  unsigned flit_count = get_flit_count(transaction, flit_size, header_size);
  return flit_count * flit_size;
}