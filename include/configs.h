#pragma once

#include <systemc>
#include <yaml-cpp/yaml.h>

using namespace sc_core;

class Config {
public:
  void load(const std::string &filepath, const std::set<std::string> &keys);

  template <typename T> T get(const std::string &key) const;

private:
  YAML::Node root;
};

class ConfigRegistry {
public:
  static const std::set<std::string> chiplet_specification;
  static const std::set<std::string> fpga_specification;
  static const std::map<std::string, std::set<std::string>>
      interconnect_specifications;

  static ConfigRegistry &instance() {
    static ConfigRegistry instance;
    return instance;
  }

  void add(const std::string &name, const std::string &filepath,
           const std::set<std::string> &specification) {
    Config config;
    config.load(filepath, specification);
    configs[name] = std::move(config);
  }

  const Config &get(const std::string &name) const {
    auto it = configs.find(name);
    if (it == configs.end()) {
      throw std::runtime_error("Config not found: " + name);
    }
    return it->second;
  }

private:
  std::map<std::string, Config> configs;
};