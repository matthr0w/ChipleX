#pragma once

#include <systemc>

#include "common/protocol/ChipletExtension.h"

struct AXIUtils {
  unsigned int axi_width_bytes;

  AXIUtils(unsigned int axi_width_bytes) : axi_width_bytes(axi_width_bytes) {}

  void set_burst_fixed(ChipletExtension *ext, size_t data_len) const {
    unsigned int beats = (data_len + axi_width_bytes - 1) / axi_width_bytes;

    std::ostringstream oss;
    unsigned int max_bytes = 16 * axi_width_bytes;
    oss << "Maximum FIXED Burst Size: " << max_bytes << " bytes";
    std::string msg = oss.str();

    if (beats < 1 || beats > 16) {
      SC_REPORT_ERROR("AXI", msg.c_str());
    }

    ext->axi_length = static_cast<uint8_t>(beats - 1);
    ext->axi_size = static_cast<uint8_t>(std::log2(axi_width_bytes));
    ext->axi_burst = 0;
  }

  void set_burst_incr(ChipletExtension *ext, size_t data_len) const {
    unsigned int beats = (data_len + axi_width_bytes - 1) / axi_width_bytes;

    std::ostringstream oss;
    unsigned int max_bytes = 256 * axi_width_bytes;
    if (max_bytes > 4096)
      max_bytes = 4096;
    oss << "Maximum INCR Burst Size: " << max_bytes << " bytes";
    std::string msg = oss.str();

    if (beats < 1 || beats > 256) {
      SC_REPORT_ERROR("AXI", msg.c_str());
    }

    ext->axi_length = static_cast<uint8_t>(beats - 1);
    ext->axi_size = static_cast<uint8_t>(std::log2(axi_width_bytes));
    ext->axi_burst = 1;
  }

  void set_burst_wrap(ChipletExtension *ext, size_t data_len) const {
    unsigned int beats = (data_len + axi_width_bytes - 1) / axi_width_bytes;

    std::ostringstream oss;
    unsigned int max_bytes = 16 * axi_width_bytes;
    oss << "Maximum WRAP Burst Size: " << max_bytes << " bytes";
    std::string msg = oss.str();

    if (beats != 2 && beats != 4 && beats != 8 && beats != 16) {
      SC_REPORT_ERROR("AXI", msg.c_str());
    }

    ext->axi_length = static_cast<uint8_t>(beats - 1);
    ext->axi_size = static_cast<uint8_t>(std::log2(axi_width_bytes));
    ext->axi_burst = 2;
  }
};