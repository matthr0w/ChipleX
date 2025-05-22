#pragma once

#include <tlm>

struct ChipletExtension : tlm::tlm_extension<ChipletExtension> {
  int request_id;
  int core_id;
  int source_id;
  int destination_id;
  int flit_count;
  int flit_id;
  int flit_padding;

  ChipletExtension(int request = -1, int core = -1, int source = -1,
                   int destination = -1, int flit_count = -1, int flit_id = -1,
                   int flit_padding = -1)
      : request_id(request), core_id(core), source_id(source),
        destination_id(destination), flit_count(flit_count), flit_id(flit_id),
        flit_padding(flit_padding) {}

  virtual tlm_extension_base *clone() const override {
    return new ChipletExtension(request_id, core_id, source_id, destination_id,
                                flit_count, flit_id, flit_padding);
  }

  virtual void copy_from(const tlm_extension_base &ext) override {
    const ChipletExtension &other = static_cast<const ChipletExtension &>(ext);
    request_id = other.request_id;
    core_id = other.core_id;
    source_id = other.source_id;
    destination_id = other.destination_id;
    flit_count = other.flit_count;
    flit_id = other.flit_id;
    flit_padding = other.flit_padding;
  }

  unsigned get_size_bytes() const {
    return sizeof(request_id) + sizeof(core_id) + sizeof(source_id) +
           sizeof(destination_id);
  }

  unsigned get_protocol_size_bytes() const {
    return sizeof(flit_count) + sizeof(flit_id) + sizeof(flit_padding);
  }
};