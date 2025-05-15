#pragma once

#include "ChipletExtension.h"

#include <tlm>

class ChipletPayload : public tlm::tlm_generic_payload {
public:
  ChipletPayload() {
    auto *ext = new ChipletExtension();
    this->set_extension(ext);
  }

  ~ChipletPayload() {
    // data buffer
    if (this->get_data_ptr()) {
      delete[] this->get_data_ptr();
    }

    // extension
    ChipletExtension *ext = nullptr;
    this->get_extension(ext);
    if (ext) {
      delete ext;
      this->clear_extension<ChipletExtension>();
    }
  }

  ChipletPayload *clone() const {
    auto *cp = new ChipletPayload();

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
    ChipletExtension *ext = nullptr;
    this->get_extension(ext);
    if (ext) {
      ChipletExtension *cp_ext = new ChipletExtension();
      cp_ext->copy_from(*ext);
      cp->set_extension(cp_ext);
    }

    return cp;
  }

  ChipletExtension *ensure_extension() {
    auto *ext = get_extension<ChipletExtension>();
    if (!ext) {
      ext = new ChipletExtension();
      set_extension(ext);
    }
    return ext;
  }

  unsigned get_size_bytes() const {
    unsigned size = 0;

    // address
    size += sizeof(uint32_t);

    // data buffer
    size += this->get_data_length();

    // extension
    ChipletExtension *ext = nullptr;
    this->get_extension(ext);
    if (ext) {
      size += ext->get_size_bytes();
    }

    return size;
  }

  unsigned get_protocol_size_bytes() const {
    unsigned size = get_size_bytes();

    // crc
    size += 2;

    return size;
  }

  unsigned get_flit_count(unsigned flit_size_bytes) const {
    unsigned total = get_protocol_size_bytes();
    return (total + flit_size_bytes - 1) / flit_size_bytes;
  }

  unsigned get_flit_bytes(unsigned flit_size_bytes) const {
    unsigned flit_count = get_flit_count(flit_size_bytes);
    return flit_count * flit_size_bytes;
  }

  void set_request_id(int id) { ensure_extension()->request_id = id; }
  void set_source_id(int id) { ensure_extension()->source_id = id; }
  void set_core_id(int id) { ensure_extension()->core_id = id; }
  void set_destination_id(int id) { ensure_extension()->destination_id = id; }
};