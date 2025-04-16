#pragma once

#include <tlm>

struct payload_extension : tlm::tlm_extension<payload_extension> {
  int request_id;
  int chiplet_id;
  int core_id;
  int destination_id;

  payload_extension(int request = -1, int chiplet = -1, int core = -1,
                    int destination = -1)
      : request_id(request), chiplet_id(chiplet), destination_id(destination) {}

  virtual tlm_extension_base *clone() const override {
    return new payload_extension(request_id, chiplet_id, core_id,
                                 destination_id);
  }

  virtual void copy_from(const tlm_extension_base &ext) override {
    const payload_extension &other =
        static_cast<const payload_extension &>(ext);
    request_id = other.request_id;
    chiplet_id = other.chiplet_id;
    core_id = other.core_id;
    destination_id = other.destination_id;
  }
};