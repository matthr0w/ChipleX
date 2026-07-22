#pragma once

#include <tlm>

#include "ARM/TLM/arm_axi4.h"

struct PayloadKey {
	unsigned request_id;
	unsigned core_id;
	unsigned source_id;

	bool operator==(const PayloadKey &other) const {
		return request_id == other.request_id && core_id == other.core_id && source_id == other.source_id;
	}
};

struct PayloadKeyHash {
	std::size_t operator()(const PayloadKey &k) const {
		return ((std::hash<unsigned>()(k.request_id) ^ (std::hash<unsigned>()(k.core_id) << 1)) >> 1) ^
		       (std::hash<unsigned>()(k.source_id) << 1);
	}
};

struct PhyRequest {
	int                       link_id;
	tlm::tlm_generic_payload *transaction;
};

struct AxiData {
	std::vector<uint8_t> data;

	AxiData(size_t size_bytes) : data(size_bytes, 0) {}
};

enum AxiChannel : uint8_t {
	None = 0,
	AW   = 1,
	W    = 2,
	AR   = 3,
	R    = 4,
	B    = 5
};

struct AxiTransaction {
	ARM::AXI::Payload *payload  = nullptr;
	AxiChannel         channel  = None;
	size_t             beat_idx = 0;
};

struct Flit {
	AxiData         axi_data;
	AxiChannel      axi_ch = None;
	uint8_t         len    = 0;
	ARM::AXI::Burst burst  = 0;
	uint32_t        id     = 0;
	uint64_t        user   = 0;

	static constexpr size_t header_size() {
		return sizeof(AxiChannel) + sizeof(uint8_t) + sizeof(ARM::AXI::Burst) + sizeof(uint32_t) + sizeof(uint64_t);
	}

	Flit(size_t data_bytes) : axi_data(data_bytes) {}
};