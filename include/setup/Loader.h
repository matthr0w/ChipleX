#pragma once

#include "setup/Types.h"

class SetupLoader {
public:
  SetupLoader(const std::string &configs_path, const std::string &setups_path,
              const std::string &setup_name) {
    interconnects_path_ = configs_path + "/interconnects/";
    chiplets_defaults_ = YAML::LoadFile(configs_path + "/chiplets.yaml");
    load_config(setups_path + "/" + setup_name + "/system.yaml");
    load_cycles(setups_path + "/" + setup_name + "/cycles.yaml");
    load_code(setups_path + "/" + setup_name + "/libsetup.so");
  }

  const SystemConfig &get_config() const { return sysconf_; }

private:
  std::string interconnects_path_;
  YAML::Node chiplets_defaults_;
  SystemConfig sysconf_;

  void load_config(const std::string &system_file);
  void load_cycles(const std::string &cycles_file);
  void load_code(const std::string &setup_lib);

  YAML::Node generate_preset_connections(
      const ConnectionPreset &preset,
      const std::map<std::string, ChipletConfig> &chiplets,
      const YAML::Node &overrides);

  void merge_nodes(YAML::Node target, const YAML::Node &override,
                   const std::string &path = "");
};