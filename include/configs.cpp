#include "configs.h"

const std::set<std::string> ConfigRegistry::chiplet_specification = {
    "cores.clk_cycle",
    "cache.size",
    "cache.block_size",
    "cache.arbitration_delay",
    "cache.access_delay",
    "axi.width",
    "axi.clk_cycle",
    "axi.arbitration_delay",
    "bus.width",
    "bus.clk_cycle",
    "bus.arbitration_delay",
    "memory_controller.address_delay",
    "ram.size",
    "ram.width",
    "ram.clk_cycle",
    "ram.access_delay"};

const std::set<std::string> ConfigRegistry::fpga_specification = {
    "cores.clk_cycle",
    "cache.size",
    "cache.block_size",
    "cache.arbitration_delay",
    "cache.access_delay",
    "axi.width",
    "axi.clk_cycle",
    "axi.arbitration_delay",
    "bus.width",
    "bus.clk_cycle",
    "bus.arbitration_delay",
    "memory_controller.address_delay",
    "ram.size",
    "ram.width",
    "ram.clk_cycle",
    "ram.access_delay"};

const std::map<std::string, std::set<std::string>>
    ConfigRegistry::interconnect_specifications{
        {"Custom",
         {"interconnect_protocol.flit_size",
          "interconnect_protocol.overhead_size",
          "interconnect_protocol.pre_delay", "interconnect_protocol.post_delay",
          "interconnect_protocol.irq_delay", "interconnect.buffer_size",
          "interconnect.bandwidth_chiplets", "interconnect.bandwidth_fpga",
          "interconnect.efficiency"}},
        {"PCIe",
         {"interconnect_protocol.flit_size",
          "interconnect_protocol.overhead_size",
          "interconnect_protocol.pre_delay", "interconnect_protocol.post_delay",
          "interconnect_protocol.fec_delay", "interconnect_protocol.irq_delay",
          "interconnect.buffer_size", "interconnect.bandwidth_chiplets",
          "interconnect.bandwidth_fpga", "interconnect.efficiency"}},
        {"UCIe",
         {"interconnect_protocol.flit_size",
          "interconnect_protocol.overhead_size",
          "interconnect_protocol.pre_delay", "interconnect_protocol.post_delay",
          "interconnect_protocol.irq_delay", "interconnect_protocol.retries",
          "interconnect.buffer_size", "interconnect.bandwidth_chiplets",
          "interconnect.bandwidth_fpga", "interconnect.efficiency"}},
        {"SPI",
         {"interconnect_protocol.flit_size",
          "interconnect_protocol.overhead_size",
          "interconnect_protocol.pre_delay", "interconnect_protocol.post_delay",
          "interconnect_protocol.irq_delay", "interconnect.buffer_size",
          "interconnect.bandwidth_chiplets", "interconnect.bandwidth_fpga",
          "interconnect.efficiency"}}};

std::vector<std::string> split_key(const std::string &key) {
  std::vector<std::string> parts;
  size_t start = 0, end;
  while ((end = key.find('.', start)) != std::string::npos) {
    parts.push_back(key.substr(start, end - start));
    start = end + 1;
  }
  parts.push_back(key.substr(start));
  return parts;
}

void Config::load(const std::string &filepath,
                  const std::set<std::string> &specification) {
  root = YAML::LoadFile(filepath);

  for (const std::string &key : specification) {
    const std::vector<std::string> parts = split_key(key);
    YAML::Node current = YAML::Clone(root);
    for (const auto &part : parts) {
      if (!current.IsMap() || !current[part]) {
        throw std::runtime_error("Missing configuration key: " + key);
      }
      current = current[part];
    }
  }
}

template <typename T> T Config::get(const std::string &key) const {
  const std::vector<std::string> parts = split_key(key);
  YAML::Node current = YAML::Clone(root);
  for (const auto &part : parts) {
    if (!current.IsMap() || !current[part]) {
      throw std::runtime_error("Missing configuration key: " + key);
    }
    current = current[part];
  }

  try {
    return current.as<T>();
  } catch (const YAML::BadConversion &e) {
    throw std::runtime_error("Invalid type for key: " + key);
  }
}

template unsigned int Config::get<unsigned int>(const std::string &) const;
template int Config::get<int>(const std::string &) const;
template double Config::get<double>(const std::string &) const;
template <>
sc_core::sc_time Config::get<sc_core::sc_time>(const std::string &key) const {
  double value = get<double>(key);
  return sc_core::sc_time(value, sc_core::SC_NS);
}