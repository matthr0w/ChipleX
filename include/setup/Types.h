#pragma once

#include <functional>
#include <map>
#include <string>
#include <tlm>
#include <utility>
#include <vector>
#include <yaml-cpp/yaml.h>

using namespace tlm;

class Core;

struct ChipletType {
  enum class Type { SingleCore, DualCore, QuadCore, Memory, Undefined };

  Type type;

  ChipletType() = default;
  ChipletType(Type t) : type(t) {}

  std::string to_string() const {
    switch (type) {
    case Type::SingleCore:
      return "single-core";
    case Type::DualCore:
      return "dual-core";
    case Type::QuadCore:
      return "quad-core";
    case Type::Memory:
      return "memory";
    default:
      return "undefined";
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
    return Type::Undefined;
  }
};

struct InterconnectType {
  enum class Type { PCIe, UCIe, SerialLink, SPI, Undefined };

  Type type;

  InterconnectType() = default;
  InterconnectType(Type t) : type(t) {}

  std::string to_string() const {
    switch (type) {
    case Type::PCIe:
      return "pcie";
    case Type::UCIe:
      return "ucie";
    case Type::SerialLink:
      return "serial-link";
    case Type::SPI:
      return "spi";
    default:
      return "undefined";
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
    return Type::Undefined;
  }
};

struct ConnectionPreset {
  enum class Type { Mesh, Ring, Star, Undefined };

  Type type;

  ConnectionPreset() = default;
  ConnectionPreset(Type t) : type(t) {}

  std::string to_string() const {
    switch (type) {
    case Type::Mesh:
      return "mesh";
    case Type::Ring:
      return "ring";
    case Type::Star:
      return "star";
    default:
      return "undefined";
    }
  }

  static Type parse(const std::string &str) {
    if (str == "mesh")
      return Type::Mesh;
    if (str == "ring")
      return Type::Ring;
    if (str == "star")
      return Type::Star;
    return Type::Undefined;
  }
};

struct ConnectionEndpoint {
  std::string chiplet_name;
  int chiplet_id;
  int link_id;
};

struct ConnectionMapping {
  ConnectionEndpoint endpoint0;
  ConnectionEndpoint endpoint1;
};

struct ChipletConnectionConfig {
  InterconnectType type;
  YAML::Node config;
  double wire_length;
  double ber_scalar;
};

struct ChipletConfig {
  ChipletType type;
  YAML::Node config;
  std::vector<ChipletConnectionConfig> connections;
};

struct InterconnectConfig {
  InterconnectType type;
  YAML::Node config;
  std::vector<ConnectionMapping> connections;
};

struct CyclesDB {
  std::unordered_map<std::string, unsigned> cycles;

  unsigned get(const std::string &name) const {
    auto it = cycles.find(name);
    return it != cycles.end() ? it->second : 0;
  }
};

using CoreFunctions = std::pair<
    std::function<void(Core &, const CyclesDB &)>,
    std::function<void(Core &, const CyclesDB &, tlm_generic_payload *)>>;
using CoreKey = std::pair<std::string, int>;
using CoreCodeMap = std::map<CoreKey, CoreFunctions>;

struct SystemConfig {
  std::map<std::string, ChipletConfig> chiplets;
  std::vector<std::string> chiplet_order;
  InterconnectConfig interconnect;
  CoreCodeMap program_code;
  CyclesDB cycles_db;
};