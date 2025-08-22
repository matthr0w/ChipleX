#pragma once

#include <tlm>

#include "ChipletExtension.h"

class ChipletPayload : public tlm::tlm_generic_payload {
private:
  bool owns_data_ptr;

public:
  ChipletPayload() : owns_data_ptr(true) {
    auto *ext = new ChipletExtension();
    this->set_extension(ext);
  }

  ~ChipletPayload() {
    if (owns_data_ptr && this->get_data_ptr()) {
      delete[] this->get_data_ptr();
    }

    ChipletExtension *ext = get_extension<ChipletExtension>();
    if (ext) {
      delete ext;
      this->clear_extension<ChipletExtension>();
    }
  }

  void set_data_ptr(unsigned char *ptr, bool take_ownership = true) {
    tlm::tlm_generic_payload::set_data_ptr(ptr);
    owns_data_ptr = take_ownership;
  }

  ChipletPayload *clone() const {
    auto *cp = new ChipletPayload();

    cp->set_command(this->get_command());
    cp->set_address(this->get_address());

    if (this->get_data_ptr() && this->get_data_length() > 0) {
      if (owns_data_ptr) {
        unsigned char *cp_data = new unsigned char[this->get_data_length()];
        std::memcpy(cp_data, this->get_data_ptr(), this->get_data_length());
        cp->set_data_ptr(cp_data, true);
        cp->set_data_length(this->get_data_length());
      } else {
        cp->set_data_ptr(this->get_data_ptr(), false);
        cp->set_data_length(this->get_data_length());
      }
    }

    ChipletExtension *ext = get_extension<ChipletExtension>();
    if (ext) {
      ChipletExtension *cp_ext = new ChipletExtension();
      cp_ext->copy_from(*ext);
      cp->set_extension(cp_ext);
    }

    return cp;
  }

  ChipletPayload *clone_ext() const {
    auto *cp = new ChipletPayload();

    ChipletExtension *ext = get_extension<ChipletExtension>();
    if (ext) {
      ChipletExtension *cp_ext = new ChipletExtension();
      cp_ext->copy_from(*ext);
      cp->set_extension(cp_ext);
    }

    return cp;
  }

  ChipletExtension *ensure_extension() {
    ChipletExtension *ext = get_extension<ChipletExtension>();
    if (!ext) {
      ext = new ChipletExtension();
      set_extension(ext);
    }

    return ext;
  }

  // AXI signals
  void set_axi_length(uint8_t value) { ensure_extension()->axi_length = value; }
  uint8_t get_axi_length() {
    ChipletExtension *ext = get_extension<ChipletExtension>();
    return ext->axi_length;
  }
  void set_axi_size(uint8_t value) { ensure_extension()->axi_size = value; }
  uint8_t get_axi_size() {
    ChipletExtension *ext = get_extension<ChipletExtension>();
    return ext->axi_size;
  }
  void set_axi_burst(uint8_t value) { ensure_extension()->axi_burst = value; }
  uint8_t get_axi_burst() {
    ChipletExtension *ext = get_extension<ChipletExtension>();
    return ext->axi_burst;
  }

  // chiplet metadata
  void set_request_id(int id) { ensure_extension()->request_id = id; }
  void set_core_id(int id) { ensure_extension()->core_id = id; }
  void set_source_id(int id) { ensure_extension()->source_id = id; }
  void set_destination_id(int id) { ensure_extension()->destination_id = id; }
  void set_fixed_address(int flag) { ensure_extension()->fixed_address = flag; }
  void set_volatile(int flag) { ensure_extension()->is_volatile = flag; }
  // flit metadata
  void set_flit_count(int count) { ensure_extension()->flit_count = count; }
  void set_flit_id(int id) { ensure_extension()->flit_id = id; }
  void set_flit_padding(int count) { ensure_extension()->flit_padding = count; }
  // transfer result
  void set_transfer_result(bool flag) { ensure_extension()->success = flag; }
};