#pragma once

#include <iomanip>
#include <iostream>

#include "modules/extensions/ExtensionBase.h"
#include "modules/extensions/ExtensionIDs.h"

class CryptoExtension : public ExtensionBase {
public:
  explicit CryptoExtension(unsigned axi_width)
      : ExtensionBase(axi_width), beat_bytes(axi_width / 8) {}

  uint8_t id() const override { return SmartExtension::CRYPTO; }

  bool can_accept() const override { return fifo.size() < 16; }

  void push(const AxiBeat &beat) override {
    if (beat.dir == AxiDir::DOWNSTREAM) {
      encrypt(beat);
    } else {
      decrypt(beat);
    }
    fifo.push_back(beat);
  }

  bool has_output() const override { return !fifo.empty(); }

  AxiBeat peek() const override { return fifo.front(); }

  AxiBeat pop() override {
    AxiBeat beat = fifo.front();
    fifo.pop_front();
    return beat;
  }

private:
  static constexpr uint64_t KEY = 0xA5A5A5A5A5A5A5A5ULL;

  void encrypt(const AxiBeat &beat) { process_beat(beat); }

  void decrypt(const AxiBeat &beat) { process_beat(beat); }

  void process_beat(const AxiBeat &beat) {
    // Addresses
    if (beat.phase == ARM::AXI::AW_VALID || beat.phase == ARM::AXI::AR_VALID) {
      uint64_t addr = beat.payload->get_address();
      addr ^= KEY;
      beat.payload->set_address(addr);
      return;
    }

    // Write data
    if (beat.phase == ARM::AXI::W_VALID ||
        beat.phase == ARM::AXI::W_VALID_LAST) {
      std::vector<uint8_t> data(beat_bytes);
      beat.payload->write_out_beat(beat.index, data.data());

      dump_data("Before crypto", data);

      for (unsigned i = 0; i < beat_bytes; ++i)
        data[i] ^= static_cast<uint8_t>(KEY >> ((i % 8) * 8));

      dump_data("After crypto", data);

      beat.payload->modify_beat(beat.index, data.data());
    }

    // Read data
    else if (beat.phase == ARM::AXI::R_VALID ||
             beat.phase == ARM::AXI::R_VALID_LAST) {
      std::vector<uint8_t> data(beat_bytes);
      beat.payload->read_out_beat(beat.index, data.data());

      dump_data("Before crypto", data);

      for (unsigned i = 0; i < beat_bytes; ++i)
        data[i] ^= static_cast<uint8_t>(KEY >> ((i % 8) * 8));

      dump_data("After crypto", data);

      beat.payload->modify_beat(beat.index, data.data());
    }
  }

  void dump_data(const char *tag, const std::vector<uint8_t> &data) const {
    std::cout << tag << ": ";
    for (auto b : data)
      std::cout << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(b) << " ";
    std::cout << std::dec << "\n";
  }

  std::deque<AxiBeat> fifo;
  const unsigned beat_bytes;
};