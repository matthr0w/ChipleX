#pragma once

#include <systemc>

#include "protocol/ChipletExtension.h"

using namespace sc_core;
using namespace tlm;

inline bool is_request(const ChipletExtension *ext) {
  return ext && (ext->destination_id != ext->source_id);
}

inline unsigned get_payload_bytes(tlm_generic_payload &transaction) {
  unsigned size = 0;

  ChipletExtension *ext = nullptr;
  transaction.get_extension(ext);

  // address only in requests
  if (is_request(ext)) {
    size += sizeof(uint32_t);
  }

  // data
  size += transaction.get_data_length();

  // extension
  if (ext) {
    size += ext->get_size_bytes();
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