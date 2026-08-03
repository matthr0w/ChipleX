#pragma once

#include <iomanip>
#include <sstream>
#include <string>

// Pulls in arm_axi4_utils.h, which has no include guard of its own.
#include "ARM/TLM/arm_axi4.h"

// Shared AXI trace formatting, so every module observing AXI phases emits the
// same field layout.
namespace AxiTrace {

inline const char *channel_name(ARM::AXI::Phase phase) {
	switch (phase) {
	case ARM::AXI::AW_VALID:
	case ARM::AXI::AW_READY:
		return "AW";
	case ARM::AXI::W_VALID:
	case ARM::AXI::W_VALID_LAST:
	case ARM::AXI::W_READY:
		return "W ";
	case ARM::AXI::B_VALID:
	case ARM::AXI::B_READY:
		return "B ";
	case ARM::AXI::AR_VALID:
	case ARM::AXI::AR_READY:
		return "AR";
	case ARM::AXI::R_VALID:
	case ARM::AXI::R_VALID_LAST:
	case ARM::AXI::R_READY:
		return "R ";
	default:
		return "??";
	}
}

inline const char *burst_name(ARM::AXI::Burst burst) {
	switch (burst) {
	case ARM::AXI::BURST_FIXED:
		return "FIXED";
	case ARM::AXI::BURST_INCR:
		return "INCR ";
	case ARM::AXI::BURST_WRAP:
		return "WRAP ";
	default:
		return "?????";
	}
}

inline const char *resp_name(ARM::AXI::Resp resp) {
	switch (resp) {
	case ARM::AXI::RESP_OKAY:
		return "OKAY  ";
	case ARM::AXI::RESP_EXOKAY:
		return "EXOKAY";
	case ARM::AXI::RESP_SLVERR:
		return "SLVERR";
	case ARM::AXI::RESP_DECERR:
		return "DECERR";
	default:
		return "??????";
	}
}

// src_module and dst_module are per-hop routing selectors, not endpoints: the bus
// routes to src_module while the payload is on the source chiplet and to
// dst_module on the destination chiplet. VIA is therefore only meaningful
// off-chip, and its id belongs to the FROM chiplet.
inline std::string identity(const ARM::AXI::Payload &payload) {
	const UserSignals  user = UserSignals::decode(payload.user);
	std::ostringstream out;
	out << "ID:" << payload.id << " UID:" << payload.uid << " CORE:" << unsigned(user.core) << " FROM:C"
	    << unsigned(user.src_chiplet) << " TO:C" << unsigned(user.dst_chiplet) << ".M" << unsigned(user.dst_module);

	if (user.src_chiplet != user.dst_chiplet) {
		out << " VIA:M" << unsigned(user.src_module);
	}

	return out.str();
}

inline std::string attributes(const ARM::AXI::Payload &payload) {
	const UserSignals  user = UserSignals::decode(payload.user);
	std::ostringstream out;
	out << std::hex << "CACHE:0x" << unsigned(payload.cache) << " PROT:0x" << unsigned(payload.prot) << std::dec
	    << " LOCK:" << unsigned(payload.lock) << " QOS:" << unsigned(payload.qos) << std::hex << " EXT:0x"
	    << unsigned(user.extension_mask) << std::dec << " FIXED_ADDR:" << unsigned(user.fixed_address);
	return out.str();
}

inline std::string addressing(const ARM::AXI::Payload &payload) {
	std::ostringstream out;
	out << "@0x" << std::right << std::setw(8) << std::setfill('0') << std::hex << payload.get_address() << std::dec
	    << ' ';

	if (payload.get_command() != ARM::AXI::COMMAND_SNOOP) {
		out << payload.get_beat_count() << "x" << (8 * (1 << payload.get_size())) << "bits "
		    << burst_name(payload.get_burst()) << ' ';
	}

	return out.str();
}

// Most significant byte first, masked bytes as XX. Scratch holds one beat.
inline std::string beat_dump(ARM::AXI::Payload &payload, unsigned beat_index, uint8_t *scratch) {
	uint64_t byte_strobe(uint64_t(~0));
	bool     has_strobe = false;

	switch (payload.get_command()) {
	case ARM::AXI::COMMAND_WRITE:
		payload.write_out_beat(beat_index, scratch);
		byte_strobe = payload.write_out_beat_strobe(beat_index);
		has_strobe  = true;
		break;
	case ARM::AXI::COMMAND_READ:
		payload.read_out_beat(beat_index, scratch);
		break;
	case ARM::AXI::COMMAND_SNOOP:
		payload.snoop_out_beat(beat_index, scratch);
		break;
	default:
		break;
	}

	std::ostringstream out;
	out << std::uppercase << std::hex;

	unsigned size = 1 << payload.get_size();
	for (int i = size - 1; i >= 0; i--) {
		if ((byte_strobe >> (i % 8)) & 1) {
			out << std::setw(2) << std::setfill('0') << unsigned(scratch[i]);
		} else {
			out << "XX";
		}
		if (i != 0 && !(i % 8)) {
			out << "_";
		}
	}

	out << std::nouppercase << std::dec;

	// Read data has no write strobe.
	if (has_strobe) {
		const uint64_t mask = (size >= 64) ? ~uint64_t(0) : ((uint64_t(1) << size) - 1);
		out << " STRB:0x" << std::hex << (byte_strobe & mask) << std::dec;
	}

	return out.str();
}

} // namespace AxiTrace
