#pragma once

#include "ARM/TLM/arm_axi4.h"

struct AxiBeat {
  std::vector<uint8_t> data;
  uint32_t addr = 0; // Store AW/AR addresses in uint32_t instead of byte vector

  AxiBeat(size_t axi_width_bytes) : data(axi_width_bytes, 0) {}
};

struct AxiTrans_t {
  ARM::AXI::Payload *w_payload = nullptr;
  ARM::AXI::Payload *r_payload = nullptr;
  ARM::AXI::Phase req_phase = ARM::AXI4::PHASE_UNINITIALIZED;
  ARM::AXI::Phase rsp_phase = ARM::AXI4::PHASE_UNINITIALIZED;
  unsigned w_beat_count = 0;
  unsigned r_beat_count = 0;

  bool operator==(const AxiTrans_t &other) const {
    return w_payload == other.w_payload && r_payload == other.r_payload &&
           req_phase == other.req_phase && rsp_phase == other.rsp_phase &&
           w_beat_count == other.w_beat_count &&
           r_beat_count == other.r_beat_count;
  }
};

enum Tag_e : uint8_t { TagIdle = 0, TagAW = 1, TagW = 2, TagAR = 3, TagR = 4 };

struct Payload_t {
  AxiBeat axi_ch;
  bool b_valid = false;
  ARM::AXI::Resp b = ARM::AXI::RESP_OKAY;
  Tag_e hdr = TagIdle;
  int credit = 0;

  Payload_t(size_t axi_width) : axi_ch((axi_width + 7) / 8) {}
};

inline std::ostream &operator<<(std::ostream &os, const AxiTrans_t &t) {
  os << "AxiTrans_t{"
     << "w_beat_count=" << t.w_beat_count << ", r_beat_count=" << t.r_beat_count
     << ", w_payload=" << t.w_payload << ", r_payload=" << t.r_payload << "}";
  return os;
}