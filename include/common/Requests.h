#pragma once

#include <cstring>
#include <optional>
#include <systemc>
#include <vector>

#include "ARM/TLM/arm_axi4.h"
#include "modules/extensions/ExtensionIDs.h"

using namespace sc_core;

struct AxiRequest {
	uint32_t       request_id;
	unsigned char *data;
	unsigned       data_length;

	std::optional<uint32_t>    address;
	std::optional<std::string> src_module_name;
	std::optional<std::string> dst_chiplet_name;
	std::optional<std::string> dst_module_name;

	ARM::AXI::Burst                   burst = ARM::AXI::BURST_INCR;
	std::optional<SmartExtension::ID> ext_id;

	AxiRequest(uint32_t id, unsigned char *buf, unsigned len) : request_id(id), data(buf), data_length(len) {}

	AxiRequest &set_addr(uint32_t addr) {
		address = addr;
		return *this;
	}

	AxiRequest &to(const std::string &module_name) {
		src_module_name = module_name;
		return *this;
	}

	AxiRequest &to_via(const std::string &chiplet_name, const std::string &module_name,
	                   const std::string &via_module_name) {
		src_module_name  = via_module_name;
		dst_chiplet_name = chiplet_name;
		dst_module_name  = module_name;
		return *this;
	}

	AxiRequest &set_burst(ARM::AXI::Burst type) {
		burst = type;
		return *this;
	}

	AxiRequest &use_ext(SmartExtension::ID id) {
		ext_id = id;
		return *this;
	}
};

struct AxiDMARequest {
	uint32_t request_id;
	unsigned data_length;

	std::optional<std::string> src_fetch_module_name;
	std::optional<std::string> fetch_chiplet_name;
	std::optional<std::string> fetch_module_name;
	std::optional<std::string> src_target_module_name;
	std::optional<std::string> target_chiplet_name;
	std::optional<std::string> target_module_name;
	std::optional<uint32_t>    fetch_addr;
	std::optional<uint32_t>    target_addr;

	ARM::AXI::Burst                   burst = ARM::AXI::BURST_INCR;
	std::optional<SmartExtension::ID> ext_id;

	AxiDMARequest(uint32_t id, unsigned len) : request_id(id), data_length(len) {}

	AxiDMARequest &from(const std::string &chiplet_name, const std::string &module_name, const uint32_t address) {
		fetch_chiplet_name = chiplet_name;
		fetch_module_name  = module_name;
		fetch_addr         = address;
		return *this;
	}

	AxiDMARequest &from_via(const std::string &chiplet_name, const std::string &module_name, const uint32_t address,
	                        const std::string &via_module_name) {
		src_fetch_module_name = via_module_name;
		fetch_chiplet_name    = chiplet_name;
		fetch_module_name     = module_name;
		fetch_addr            = address;
		return *this;
	}

	AxiDMARequest &to(const std::string &chiplet_name, const std::string &module_name, const uint32_t address) {
		target_chiplet_name = chiplet_name;
		target_module_name  = module_name;
		target_addr         = address;
		return *this;
	}

	AxiDMARequest &to_via(const std::string &chiplet_name, const std::string &module_name, const uint32_t address,
	                      const std::string &via_module_name) {
		src_target_module_name = via_module_name;
		target_chiplet_name    = chiplet_name;
		target_module_name     = module_name;
		target_addr            = address;
		return *this;
	}

	AxiDMARequest &set_burst(ARM::AXI::Burst type) {
		burst = type;
		return *this;
	}

	AxiDMARequest &use_ext(SmartExtension::ID id) {
		ext_id = id;
		return *this;
	}
};

struct RequestHandle {
	ARM::AXI::Payload *payload;
	unsigned char     *data;
	unsigned           data_length = 0;

	bool     completed = false;
	sc_time  time_stamp;
	sc_event done;

	RequestHandle() : payload(nullptr) {}

	void notify(sc_time delay) {
		completed = true;
		if (payload->get_command() == ARM::AXI::COMMAND_READ) {
			const unsigned aligned = static_cast<unsigned>(payload->get_data_length());
			if (data_length == aligned) {
				payload->read_out(data);
			} else {
				std::vector<uint8_t> bounce(aligned);
				payload->read_out(bounce.data());
				std::memcpy(data, bounce.data(), data_length);
			}
		}
		done.notify(delay);
	}

	void wait() {
		if (!completed) {
			// Qualified with sc_core rather than escaping to the global
			// scope. This struct's own wait() member hides the unqualified
			// name, so a qualification is required; ::wait would bind to
			// POSIX wait(int *) on any platform whose standard headers pull
			// in <sys/wait.h>, because finding that declaration directly in
			// the global namespace ends lookup before the using-directive
			// above brings sc_core::wait into view.
			sc_core::wait(done);
		}
	}
};