#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>

struct ChipletType {
  enum class Type { SingleCore, DualCore, QuadCore, Memory, Unknown };

  Type type;

  ChipletType() = default;
  ChipletType(Type t) : type(t) {}

  std::string str() const {
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

struct ChipletConfig {
  ChipletType type;
  YAML::Node config;
  YAML::Node interconnect_overrides;
};

struct InterconnectConnection {
  std::string from;
  std::string to;
  int distance_um;
};

struct InterconnectType {
  enum class Type { PCIe, UCIe, SerialLink, SPI, Unknown };

  Type type;

  InterconnectType() = default;
  InterconnectType(Type t) : type(t) {}

  std::string str() const {
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

struct InterconnectConfig {
  InterconnectType type;
  YAML::Node config;
  std::vector<InterconnectConnection> connections;
};

struct SystemConfig {
  std::unordered_map<std::string, ChipletConfig> chiplets;
  InterconnectConfig interconnect;
};

class SystemLoader {
public:
  SystemLoader(const std::string &system_yaml,
               const std::string &configs_path) {
    interconnects_path_ = configs_path + "/interconnects/";
    chiplets_yaml_ = configs_path + "/chiplets.yaml";
    chiplets_defaults_ = YAML::LoadFile(chiplets_yaml_);
    load(system_yaml);
  }

  const SystemConfig &get_system() const { return system_; }

private:
  std::string interconnects_path_;
  std::string chiplets_yaml_;
  YAML::Node chiplets_defaults_;
  SystemConfig system_;

  void load(const std::string &system_yaml);

  void merge_nodes(YAML::Node target, const YAML::Node &override,
                   const std::string &path = "");
};