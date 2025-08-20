#pragma once

#include <systemc>
#include <tlm>

using namespace sc_core;

struct ChipletExtension : tlm::tlm_extension<ChipletExtension> {
  // statistics
  sc_time start_time;
  // AXI signals
  uint8_t axi_length = 1;
  uint8_t axi_size = 1;
  uint8_t axi_burst = 1;
  // chiplet metadata
  int request_id = -1;
  int core_id = -1;
  int source_id = -1;
  int destination_id = -1;
  bool fixed_address = true;
  bool is_volatile = false;
  // flit metadata
  int flit_count = -1;
  int flit_id = -1;
  int flit_padding = -1;
  // transfer result
  bool success = true;

  ChipletExtension(sc_time start_time = sc_time_stamp())
      : start_time(start_time) {}

  virtual tlm_extension_base *clone() const override {
    return new ChipletExtension(start_time);
  }

  virtual void copy_from(const tlm_extension_base &ext) override {
    const ChipletExtension &other = static_cast<const ChipletExtension &>(ext);
    start_time = other.start_time;
    axi_length = other.axi_length;
    axi_size = other.axi_size;
    axi_burst = other.axi_burst;
    request_id = other.request_id;
    core_id = other.core_id;
    source_id = other.source_id;
    destination_id = other.destination_id;
    fixed_address = other.fixed_address;
    is_volatile = other.is_volatile;
    flit_count = other.flit_count;
    flit_id = other.flit_id;
    flit_padding = other.flit_padding;
    success = other.success;
  }

  unsigned get_stdext_size_bytes() const {
    return sizeof(request_id) + sizeof(core_id) + sizeof(source_id) +
           sizeof(destination_id) + sizeof(fixed_address) + sizeof(is_volatile);
  }

  unsigned get_flitext_size_bytes() const {
    return sizeof(flit_count) + sizeof(flit_id) + sizeof(flit_padding);
  }
};