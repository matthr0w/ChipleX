#pragma once

#include "setup/Types.h"

class SetupLoader {
public:
  SetupLoader(const std::string &configs_path, const std::string &setups_path,
              const std::string &setup_name) {
    chiplets_path_ = configs_path + "/chiplets/";
    interconnects_path_ = configs_path + "/interconnects/";
    load_system_config(setups_path + "/" + setup_name + "/system.yaml");
    load_cycles_db(setups_path + "/" + setup_name + "/workloads.yaml");
    load_lib_code(setups_path + "/" + setup_name + "/libsetup.so");
  }

  const SystemConfig &get_config() const { return sysconf_; }

private:
  std::string chiplets_path_;
  std::string interconnects_path_;
  SystemConfig sysconf_;

  void load_system_config(const std::string &system_file);
  void load_cycles_db(const std::string &workloads_file);
  void load_lib_code(const std::string &setup_lib);

  void merge_nodes(YAML::Node target, const YAML::Node &override,
                   const std::string &path = "");
};