#pragma once

#include <functional>
#include <map>
#include <string>
#include <tlm>
#include <utility>
#include <vector>
#include <yaml-cpp/yaml.h>

#include "common/IRQ.h"

using namespace tlm;

class Core;

struct ChipletType {
  enum class Type { Unknown, SingleCore, DualCore, QuadCore, Memory };

  Type value;

  ChipletType() = default;
  ChipletType(Type type) : value(type) {}

  std::string to_string() const {
    switch (value) {
    case Type::SingleCore:
      return "single-core";
    case Type::DualCore:
      return "dual-core";
    case Type::QuadCore:
      return "quad-core";
    case Type::Memory:
      return "memory";
    default:
      return "unknown";
    }
  }

  static Type parse(const std::string &str) {
    if (str == "single-core")
      return Type::SingleCore;
    if (str == "dual-core")
      return Type::DualCore;
    if (str == "quad-core")
      return Type::QuadCore;
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

struct ConnectionPreset {
  enum class Type { Unknown, Mesh, Ring, Star };

  Type type;

  ConnectionPreset() = default;
  ConnectionPreset(Type type) : type(type) {}

  std::string to_string() const {
    switch (type) {
    case Type::Mesh:
      return "mesh";
    case Type::Ring:
      return "ring";
    case Type::Star:
      return "star";
    default:
      return "unknown";
    }
  }

  static Type parse(const std::string &str) {
    if (str == "mesh")
      return Type::Mesh;
    if (str == "ring")
      return Type::Ring;
    if (str == "star")
      return Type::Star;
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

using CoreFunctions = std::pair<std::function<void(Core &)>,
                                std::function<void(Core &, const IRQ &)>>;
using CoreKey = std::pair<std::string, int>;
using CoreCodeMap = std::map<CoreKey, CoreFunctions>;

struct SystemConfig {
  std::map<std::string, ChipletConfig> chiplets;
  std::map<std::string, unsigned> chiplet_ids;
  std::vector<ConnectionMapping> connections;
  CoreCodeMap program_code;
  CyclesDB cycles;
};