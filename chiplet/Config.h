#pragma once

#include <string>
#include <systemc>

#include <yaml-cpp/yaml.h>

using namespace sc_core;

namespace chiplet {
class Config {
public:
  static Config &instance();

  void load(const std::string &filename);
  void override(const std::string &filename);
  void print() const;

  // accessors
  unsigned int busWidth() const { return bus_width; }
  sc_time busClkCycle() const { return bus_clk_cycle; }
  sc_time busArbitrationDelay() const { return bus_arbitration_delay; }

  unsigned int ramSize() const { return ram_size; }
  unsigned int ramWidth() const { return ram_width; }
  sc_time ramClkCycle() const { return ram_clk_cycle; }
  sc_time ramAccessDelay() const { return ram_access_delay; }

  unsigned int interconnectProtocolBufferSize() const {
    return interconnect_protocol_buffer_size;
  }
  unsigned int interconnectProtocolFlitSize() const {
    return interconnect_protocol_flit_size;
  }
  unsigned int interconnectProtocolHeaderSize() const {
    return interconnect_protocol_header_size;
  }
  sc_time interconnectProtocolPreDelay() const {
    return interconnect_protocol_pre_delay;
  }
  sc_time interconnectProtocolPostDelay() const {
    return interconnect_protocol_post_delay;
  }
  sc_time interconnectProtocolIRQDelay() const {
    return interconnect_protocol_irq_delay;
  }

  unsigned int interconnectBufferSize() const {
    return interconnect_buffer_size;
  }
  double interconnectBandwidth() const { return interconnect_bandwidth; }

private:
  Config() = default;

  void validate(const YAML::Node &config);

  // internals
  unsigned int bus_width;
  sc_time bus_clk_cycle;
  sc_time bus_arbitration_delay;

  unsigned int ram_size;
  unsigned int ram_width;
  sc_time ram_clk_cycle;
  sc_time ram_access_delay;

  unsigned int interconnect_protocol_buffer_size;
  unsigned int interconnect_protocol_flit_size;
  unsigned int interconnect_protocol_header_size;
  sc_time interconnect_protocol_pre_delay;
  sc_time interconnect_protocol_post_delay;
  sc_time interconnect_protocol_irq_delay;

  unsigned int interconnect_buffer_size;
  double interconnect_bandwidth;
};
} // namespace chiplet