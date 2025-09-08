#include "arm_axi4_payload.h"
#include "arm_axi4_phase.h"

struct UserSignals {
  uint8_t core = 0;
  uint8_t source = 0;
  uint8_t destination = 0;
  bool fixed_address = true;
  uint16_t flit_count = 0;

  uint64_t encode() const {
    uint64_t val = 0;
    val |= (static_cast<uint64_t>(core) & 0xFF) << 56;
    val |= (static_cast<uint64_t>(source) & 0xFF) << 48;
    val |= (static_cast<uint64_t>(destination) & 0xFF) << 40;
    val |= (static_cast<uint64_t>(fixed_address) & 0x1) << 39;
    val |= (static_cast<uint64_t>(flit_count) & 0xFFFF) << 23;
    return val;
  }

  static UserSignals decode(uint64_t val) {
    UserSignals user;
    user.core = static_cast<uint8_t>((val >> 56) & 0xFF);
    user.source = static_cast<uint8_t>((val >> 48) & 0xFF);
    user.destination = static_cast<uint8_t>((val >> 40) & 0xFF);
    user.fixed_address = ((val >> 39) & 0x1) != 0;
    user.flit_count = static_cast<uint16_t>((val >> 23) & 0xFFFF);
    return user;
  }
};

inline ARM::AXI::Size get_axi_size(unsigned bits) {
  if (bits < 8 || (bits % 8) != 0)
    throw std::invalid_argument("AXI size must be a multiple of 8 and >= 8");

  unsigned bytes = bits / 8;

  // compute log2(bytes)
  unsigned log2val = 0;
  while ((1u << log2val) < bytes) {
    ++log2val;
  }

  return static_cast<ARM::AXI::Size>(log2val);
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