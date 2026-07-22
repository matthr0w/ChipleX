#pragma once

#include <iomanip>
#include <iostream>

#include "globals.h"
#include "modules/extensions/ExtensionBase.h"
#include "modules/extensions/ExtensionIDs.h"

const size_t FIFO_SIZE = 16;

class CryptoExtension : public ExtensionBase {
  public:
	explicit CryptoExtension(unsigned axi_width) : ExtensionBase(axi_width), beat_bytes(axi_width / 8) {}

	uint8_t id() const override {
		return SmartExtension::CRYPTO;
	}

	bool can_accept() const override {
		return fifo_in.size() < FIFO_SIZE;
	}

	void push(const AxiBeat &beat) override {
		fifo_in.push_back(beat);
	}

	bool has_output() const override {
		return !fifo_out.empty();
	}

	AxiBeat peek() const override {
		return fifo_out.front();
	}

	AxiBeat pop() override {
		AxiBeat beat = fifo_out.front();
		fifo_out.pop_front();
		return beat;
	}

	void tick() override {
		// Phase 1: commit pipeline register to output
		if (pipe_valid) {
			fifo_out.push_back(pipe_reg);
			pipe_valid = false;
		}

		// Phase 2: accept & process one beat into pipeline register
		if (!pipe_valid && !fifo_in.empty()) {
			pipe_reg = fifo_in.front();
			fifo_in.pop_front();

			if (pipe_reg.dir == AxiDir::DOWNSTREAM) {
				encrypt(pipe_reg);
			} else {
				decrypt(pipe_reg);
			}

			pipe_valid = true;
		}
	}

  private:
	static constexpr uint64_t KEY = 0xA5A5A5A5A5A5A5A5ULL;

	void encrypt(const AxiBeat &beat) {
		process_beat(beat);
	}

	void decrypt(const AxiBeat &beat) {
		process_beat(beat);
	}

	void process_beat(const AxiBeat &beat) {
		// Addresses
		if (beat.phase == ARM::AXI::AW_VALID || beat.phase == ARM::AXI::AR_VALID) {
			uint64_t addr  = beat.payload->get_address();
			addr          ^= KEY;
			beat.payload->set_address(addr);
			return;
		}

		// Write data
		if (beat.phase == ARM::AXI::W_VALID || beat.phase == ARM::AXI::W_VALID_LAST) {
			std::vector<uint8_t> data(beat_bytes);
			beat.payload->write_out_beat(beat.index, data.data());

			dump_data("Before crypto", data);

			for (unsigned i = 0; i < beat_bytes; ++i) {
				data[i] ^= static_cast<uint8_t>(KEY >> ((i % 8) * 8));
			}

			dump_data("After crypto", data);

			beat.payload->modify_beat(beat.index, data.data());
		}

		// Read data
		else if (beat.phase == ARM::AXI::R_VALID || beat.phase == ARM::AXI::R_VALID_LAST) {
			std::vector<uint8_t> data(beat_bytes);
			beat.payload->read_out_beat(beat.index, data.data());

			dump_data("Before crypto", data);

			for (unsigned i = 0; i < beat_bytes; ++i) {
				data[i] ^= static_cast<uint8_t>(KEY >> ((i % 8) * 8));
			}

			dump_data("After crypto", data);

			beat.payload->modify_beat(beat.index, data.data());
		}
	}

	void dump_data(const char *tag, const std::vector<uint8_t> &data) const {
		// Debug-only: this ran unconditionally on every W/R beat, printing hex to
		// stdout even under SILENT (a real hot-path cost when crypto is enabled).
		if (log_level > LogLevel::DEBUG) {
			return;
		}
		std::cout << tag << ": ";
		for (auto b : data) {
			std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(b) << " ";
		}
		std::cout << std::dec << "\n";
	}

	// FIFOs
	std::deque<AxiBeat> fifo_in;
	std::deque<AxiBeat> fifo_out;

	// Pipeline
	AxiBeat pipe_reg;
	bool    pipe_valid = false;

	const unsigned beat_bytes;
};