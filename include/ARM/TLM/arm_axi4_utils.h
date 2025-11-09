#include "arm_axi4_payload.h"
#include "arm_axi4_phase.h"

struct UserSignals {
  uint8_t core = 0;
  uint8_t src_chiplet = 0;
  uint8_t dst_chiplet = 0;
  uint8_t src_module = 0;
  uint8_t dst_module = 0;
  bool fixed_address = true;

  uint64_t encode() const {
    uint64_t val = 0;
    val |= (static_cast<uint64_t>(core) & 0xFF) << 56;
    val |= (static_cast<uint64_t>(src_chiplet) & 0xFF) << 48;
    val |= (static_cast<uint64_t>(dst_chiplet) & 0xFF) << 40;
    val |= (static_cast<uint64_t>(src_module) & 0xFF) << 32;
    val |= (static_cast<uint64_t>(dst_module) & 0xFF) << 24;
    val |= (static_cast<uint64_t>(fixed_address) & 0x1) << 23;
    return val;
  }

  static UserSignals decode(uint64_t val) {
    UserSignals user;
    user.core = static_cast<uint8_t>((val >> 56) & 0xFF);
    user.src_chiplet = static_cast<uint8_t>((val >> 48) & 0xFF);
    user.dst_chiplet = static_cast<uint8_t>((val >> 40) & 0xFF);
    user.src_module = static_cast<uint8_t>((val >> 32) & 0xFF);
    user.dst_module = static_cast<uint8_t>((val >> 24) & 0xFF);
    user.fixed_address = ((val >> 23) & 0x1) != 0;
    return user;
  }
};

inline ARM::AXI::Size get_axi_size(unsigned bits) {
  unsigned bytes = bits / 8;

  // Compute log2(bytes)
  unsigned log2val = 0;
  while ((1u << log2val) < bytes) {
    ++log2val;
  }

  return static_cast<ARM::AXI::Size>(log2val);
}

inline std::string get_axi_phase_string(ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AW_VALID:
    return "AW_VALID";
  case ARM::AXI::AW_READY:
    return "AW_READY";
  case ARM::AXI::W_VALID:
    return "W_VALID";
  case ARM::AXI::W_VALID_LAST:
    return "W_VALID_LAST";
  case ARM::AXI::W_READY:
    return "W_READY";
  case ARM::AXI::B_VALID:
    return "B_VALID";
  case ARM::AXI::B_READY:
    return "B_READY";
  case ARM::AXI::AR_VALID:
    return "AR_VALID";
  case ARM::AXI::AR_READY:
    return "AR_READY";
  case ARM::AXI::R_VALID:
    return "R_VALID";
  case ARM::AXI::R_VALID_LAST:
    return "R_VALID_LAST";
  case ARM::AXI::R_READY:
    return "R_READY";
  default:
    return "Unknown";
  }
}

inline bool is_aw_valid(ARM::AXI::Phase &phase) {
  return phase == ARM::AXI::AW_VALID;
}
inline bool is_aw_ready(ARM::AXI::Phase &phase) {
  return phase == ARM::AXI::AW_READY;
}
inline bool is_w_valid(ARM::AXI::Phase &phase) {
  return phase == ARM::AXI::W_VALID;
}
inline bool is_w_valid_last(ARM::AXI::Phase &phase) {
  return phase == ARM::AXI::W_VALID_LAST;
}
inline bool is_w_ready(ARM::AXI::Phase &phase) {
  return phase == ARM::AXI::W_READY;
}
inline bool is_b_valid(ARM::AXI::Phase &phase) {
  return phase == ARM::AXI::B_VALID;
}
inline bool is_b_ready(ARM::AXI::Phase &phase) {
  return phase == ARM::AXI::B_READY;
}
inline bool is_ar_valid(ARM::AXI::Phase &phase) {
  return phase == ARM::AXI::AR_VALID;
}
inline bool is_ar_ready(ARM::AXI::Phase &phase) {
  return phase == ARM::AXI::AR_READY;
}
inline bool is_r_valid(ARM::AXI::Phase &phase) {
  return phase == ARM::AXI::R_VALID;
}
inline bool is_r_valid_last(ARM::AXI::Phase &phase) {
  return phase == ARM::AXI::R_VALID_LAST;
}
inline bool is_r_ready(ARM::AXI::Phase &phase) {
  return phase == ARM::AXI::R_READY;
}