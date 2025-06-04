#pragma once

#include <systemc>
#include <tlm>

using namespace sc_core;

struct ChipletExtension : tlm::tlm_extension<ChipletExtension> {
  // statistics
  sc_time start_time;

  // metadata
  int request_id = -1;
  int core_id = -1;
  int source_id = -1;
  int destination_id = -1;
  bool fixed_address = true;

  // flits
  int flit_count = -1;
  int flit_id = -1;
  int flit_padding = -1;

  // transfer result
  bool success = true;

  ChipletExtension(sc_time start_time = sc_time_stamp(), int request_id = -1,
                   int core_id = -1, int source_id = -1,
                   int destination_id = -1, bool fixed_address = true,
                   int flit_count = -1, int flit_id = -1, int flit_padding = -1,
                   bool success = true)
      : start_time(start_time), request_id(request_id), core_id(core_id),
        source_id(source_id), destination_id(destination_id),
        fixed_address(fixed_address), flit_count(flit_count), flit_id(flit_id),
        flit_padding(flit_padding), success(success) {}

  virtual tlm_extension_base *clone() const override {
    return new ChipletExtension(start_time, request_id, core_id, source_id,
                                destination_id, fixed_address, flit_count,
                                flit_id, flit_padding, success);
  }

  virtual void copy_from(const tlm_extension_base &ext) override {
    const ChipletExtension &other = static_cast<const ChipletExtension &>(ext);
    start_time = other.start_time;
    request_id = other.request_id;
    core_id = other.core_id;
    source_id = other.source_id;
    destination_id = other.destination_id;
    fixed_address = other.fixed_address;
    flit_count = other.flit_count;
    flit_id = other.flit_id;
    flit_padding = other.flit_padding;
    success = other.success;
  }

  unsigned get_stdext_size_bytes() const {
    return sizeof(request_id) + sizeof(core_id) + sizeof(source_id) +
           sizeof(destination_id) + sizeof(fixed_address);
  }

  unsigned get_flitext_size_bytes() const {
    return sizeof(flit_count) + sizeof(flit_id) + sizeof(flit_padding);
  }
};