#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <yaml-cpp/yaml.h>

#include "common/IRQ.h"

class Core;
class HWAccel;

struct CPUCode {
  std::function<void(Core &)> main;
  std::function<void(Core &, const IRQ &)> irq;
};

struct AccelCode {
  std::function<void(HWAccel &, uint8_t *data, size_t size)> main;
};

using ModuleKey = std::pair<std::string, std::string>;
using ModuleFunctions = std::variant<CPUCode, AccelCode>;
using ModuleCodeMap = std::map<ModuleKey, ModuleFunctions>;

struct ChipletType {
  enum class Type { Unknown, Compute, Memory };

  Type value;

  ChipletType() = default;
  ChipletType(Type type) : value(type) {}

  std::string to_string() const {
    switch (value) {
    case Type::Compute:
      return "compute";
    case Type::Memory:
      return "memory";
    default:
      return "unknown";
    }
  }

  static Type parse(const std::string &str) {
    if (str == "compute")
      return Type::Compute;
    if (str == "memory")
      return Type::Memory;
    return Type::Unknown;
  }
};

struct InterconnectType {
  enum class Type { Unknown, PCIe, UCIe, SerialLink, SPI };

  Type value;

  InterconnectType() = default;
  InterconnectType(Type type) : value(type) {}

  std::string to_string() const {
    switch (value) {
    case Type::PCIe:
      return "pcie";
    case Type::UCIe:
      return "ucie";
    case Type::SerialLink:
      return "serial-link";
    case Type::SPI:
      return "spi";
    default:
      return "unknown";
    }
  }

  static Type parse(const std::string &str) {
    if (str == "pcie")
      return Type::PCIe;
    if (str == "ucie")
      return Type::UCIe;
    if (str == "serial-link")
      return Type::SerialLink;
    if (str == "spi")
      return Type::SPI;
    return Type::Unknown;
  }
};

struct ConnectionConfig {
  YAML::Node node;
  double wire_length;
  double ber_scalar;
};

struct InterconnectConfig {
  InterconnectType type;
  YAML::Node node;
  std::vector<ConnectionConfig> connections;
};

struct ChipletConfig {
  ChipletType type;
  YAML::Node node;
  std::map<std::string, InterconnectConfig> interconnects;
  std::map<std::string, unsigned> interconnect_ids;
  std::map<unsigned, std::string> interconnect_ids_reverse;
  std::map<std::string, ModuleFunctions> module_code;
};

struct ConnectionEndpoint {
  std::string chiplet_name;
  unsigned chiplet_id;
  std::string interconnect_name;
  unsigned interconnect_id;
  unsigned link_id;
};

struct ConnectionMapping {
  ConnectionEndpoint endpoint0;
  ConnectionEndpoint endpoint1;
};

struct CyclesDB {
  std::unordered_map<std::string, unsigned> db;

  unsigned get(const std::string &name) const {
    auto it = db.find(name);
    return it != db.end() ? it->second : 0;
  }
};

struct SystemConfig {
  std::map<std::string, ChipletConfig> chiplets;
  std::map<std::string, unsigned> chiplet_ids;
  std::vector<ConnectionMapping> connections;
  CyclesDB cycles;
};