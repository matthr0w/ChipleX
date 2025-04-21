#pragma once

#include <tlm>

struct ChipletExtension : tlm::tlm_extension<ChipletExtension> {
  int request_id;
  int source_id;
  int core_id;
  int destination_id;

  ChipletExtension(int request = -1, int source = -1, int core = -1,
                    int destination = -1)
      : request_id(request), source_id(source), destination_id(destination) {}

  virtual tlm_extension_base *clone() const override {
    return new ChipletExtension(request_id, source_id, core_id,
                                 destination_id);
  }

  virtual void copy_from(const tlm_extension_base &ext) override {
    const ChipletExtension &other =
        static_cast<const ChipletExtension &>(ext);
    request_id = other.request_id;
    source_id = other.source_id;
    core_id = other.core_id;
    destination_id = other.destination_id;
  }
};