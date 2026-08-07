#pragma once

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

#include "ARM/TLM/arm_axi4.h"

struct Committer {
	enum State {
		Idle     = 0,
		ArPend   = 1,
		AwPend   = 2,
		ArAwPend = 3
	};

	static const std::string to_string(State state) {
		switch (state) {
		case Idle:
			return "Idle";
		case ArPend:
			return "ArPend";
		case AwPend:
			return "AwPend";
		case ArAwPend:
			return "ArAwPend";
		default:
			return "Unknown";
		}
	}
};

struct AxiBeat {
	std::vector<uint8_t> data;
	uint32_t             addr = 0; // Store AW/AR addresses in uint32_t instead of byte vector

	AxiBeat(size_t axi_width_bytes) : data(axi_width_bytes, 0) {}
};

struct AxiTrans_t {
	ARM::AXI::Payload *w_payload    = nullptr;
	ARM::AXI::Payload *r_payload    = nullptr;
	ARM::AXI::Phase    w_req_phase  = ARM::AXI4::PHASE_UNINITIALIZED;
	ARM::AXI::Phase    w_rsp_phase  = ARM::AXI4::PHASE_UNINITIALIZED;
	ARM::AXI::Phase    r_req_phase  = ARM::AXI4::PHASE_UNINITIALIZED;
	ARM::AXI::Phase    r_rsp_phase  = ARM::AXI4::PHASE_UNINITIALIZED;
	bool               w_rsp_sent   = false;
	bool               r_rsp_sent   = false;
	unsigned           w_beat_count = 0;
	unsigned           r_beat_count = 0;

	bool operator==(const AxiTrans_t &other) const {
		return w_payload == other.w_payload && r_payload == other.r_payload && w_req_phase == other.w_req_phase &&
		       w_rsp_phase == other.w_rsp_phase && r_req_phase == other.r_req_phase &&
		       r_rsp_phase == other.r_rsp_phase && w_beat_count == other.w_beat_count &&
		       r_beat_count == other.r_beat_count;
	}
};

inline std::ostream &operator<<(std::ostream &os, const AxiTrans_t &t) {
	os << "AxiTrans_t{"
	   << "w_payload=" << t.w_payload << ", r_payload=" << t.r_payload << ", w_beat_count=" << t.w_beat_count
	   << ", r_beat_count=" << t.r_beat_count << "}";
	return os;
}

enum Tag_e : uint8_t {
	TagIdle = 0,
	TagAW   = 1,
	TagW    = 2,
	TagAR   = 3,
	TagR    = 4
};

inline const char *to_string(Tag_e tag) {
	switch (tag) {
	case TagIdle:
		return "IDLE";
	case TagAW:
		return "AW";
	case TagW:
		return "W";
	case TagAR:
		return "AR";
	case TagR:
		return "R";
	default:
		return "?";
	}
}

struct Payload_t {
	// AXI beat
	AxiBeat axi_ch;

	// Header
	Tag_e    hdr    = TagIdle;
	int      credit = 0;
	uint32_t id     = 0;
	uint64_t user   = 0;

	// Write response, carried alongside whatever hdr holds
	bool     b_valid = false;
	uint32_t b_id    = 0;

	// Flow control
	int link_id = 0;

	// AXI TLM payload construction
	uint8_t         len   = 0;
	ARM::AXI::Burst burst = 0;

	// Wire size of one packet in bytes. Every packet is the same size, so a data
	// beat costs as much as an address to transmit. Not sizeof(Payload_t): those
	// members are simulation state, not wire fields.
	//
	//   +-----+---------------------------+---------+-----+-----+--------+
	//   | pad |          axi_ch           | b_valid |  b  | hdr | credit |
	//   +-----+---------------------------+---------+-----+-----+--------+
	//     (a)      max(AW, AR, W, R)           1      (b)    4      4      bits
	//
	//   (a) rounds the packet up to whole bytes
	//   (b) id + resp + user
	//
	//   AW  id + addr + ctrl + atop + user
	//   AR  id + addr + ctrl + user
	//   W   data + strb + last + user
	//   R   id + data + resp + last + user
	static constexpr size_t simulation_size(size_t axi_width) {
		constexpr size_t id   = 6; // 4 manager bits plus 2 bits of crossbar port
		constexpr size_t addr = 64;
		constexpr size_t user = 1;  // a zero-width user field cannot be a packed type
		constexpr size_t ctrl = 29; // len 8, size 3, burst 2, lock 1, cache 4, prot 3, qos 4, region 4
		constexpr size_t atop = 6;
		constexpr size_t resp = 2;
		constexpr size_t last = 1;

		const size_t aw = id + addr + ctrl + atop + user;
		const size_t ar = id + addr + ctrl + user;
		const size_t w  = axi_width + axi_width / 8 + last + user; // one strobe bit per data byte
		const size_t r  = id + axi_width + resp + last + user;

		const size_t axi_ch  = std::max(std::max(aw, ar), std::max(w, r));
		const size_t framing = 1 + (id + resp + user) + 4 + 4;

		return (axi_ch + framing + 7) / 8;
	}

	Payload_t(size_t axi_width) : axi_ch((axi_width + 7) / 8) {}
};

// Shared packet description, so every link event prints the same fields.
inline std::string describe(const Payload_t &payload) {
	const UserSignals  user = UserSignals::decode(payload.user);
	std::ostringstream out;
	out << std::left << std::setw(4) << to_string(payload.hdr) << " ID:" << payload.id << " @0x" << std::hex
	    << payload.axi_ch.addr << std::dec << " LEN:" << unsigned(payload.len);

	// Responses overwrite their destination with the requester's chiplet, so
	// both USER chiplet fields are equal and only the destination is meaningful.
	if (payload.hdr == TagR || payload.b_valid) {
		out << " ->C" << unsigned(user.dst_chiplet);
	} else {
		out << " C" << unsigned(user.src_chiplet) << "->C" << unsigned(user.dst_chiplet);
	}

	if (payload.b_valid) {
		out << " +B ID:" << payload.b_id;
	}

	out << " link:" << payload.link_id << " credit:" << payload.credit;
	return out.str();
}

// Safe struct for memcpy
struct PayloadWire_t {
	// AXI beat
	uint32_t addr;
	// AXI data bytes are appended

	// Header
	Tag_e    hdr;
	int      credit;
	uint32_t id;
	uint64_t user;
	bool     b_valid;
	uint32_t b_id;

	// Flow control
	int link_id;

	// AXI TLM payload construction
	uint8_t         len;
	ARM::AXI::Burst burst;
};

constexpr size_t AXI_ADDR_WIRE_OFFSET = 0;
constexpr size_t AXI_DATA_WIRE_OFFSET = sizeof(PayloadWire_t);