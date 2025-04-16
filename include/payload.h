#pragma once

#include "payload_extension.h"

#include <tlm>

class payload : public tlm::tlm_generic_payload {
public:
  payload() {
    auto *ext = new payload_extension();
    this->set_extension(ext);
  }

  ~payload() {
    // data buffer
    if (this->get_data_ptr()) {
      delete this->get_data_ptr();
    }

    // extension
    payload_extension *ext = nullptr;
    this->get_extension(ext);
    if (ext) {
      delete ext;
      this->clear_extension<payload_extension>();
    }
  }

  payload *clone() const {
    auto *cp = new payload();

    // TLM attributes
    cp->set_command(this->get_command());
    cp->set_address(this->get_address());

    // data buffer
    if (this->get_data_ptr() && this->get_data_length() > 0) {
      unsigned char *cp_data = new unsigned char[this->get_data_length()];
      std::memcpy(cp_data, this->get_data_ptr(), this->get_data_length());
      cp->set_data_ptr(cp_data);
    }

    // data size
    cp->set_data_length(this->get_data_length());

    // extension
    payload_extension *ext = nullptr;
    this->get_extension(ext);
    if (ext) {
      payload_extension *cp_ext = new payload_extension();
      cp_ext->copy_from(*ext);
      cp->set_extension(cp_ext);
    }

    return cp;
  }

  payload_extension *ensure_extension() {
    auto *ext = get_extension<payload_extension>();
    if (!ext) {
      ext = new payload_extension();
      set_extension(ext);
    }
    return ext;
  }

  void set_request_id(int id) { ensure_extension()->request_id = id; }
  void set_chiplet_id(int id) { ensure_extension()->chiplet_id = id; }
  void set_core_id(int id) { ensure_extension()->core_id = id; }
  void set_destination_id(int id) { ensure_extension()->destination_id = id; }
};