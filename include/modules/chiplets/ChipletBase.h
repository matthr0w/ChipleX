#pragma once

#include <systemc>

#include "modules/interconnects/InterconnectBase.h"
#include "setup/Types.h"

using namespace sc_core;

struct ChipletClocks {
  YAML::Node config;

  std::map<std::string, std::unique_ptr<sc_clock>> clocks;

  ChipletClocks(const YAML::Node &config) : config(config) {}

  sc_clock &get(const std::string &name) {
    auto it = clocks.find(name);
    if (it != clocks.end())
      return *it->second;

    unsigned cycle_ns = 1;
    if (config[name] && config[name]["clk_cycle"])
      cycle_ns = config[name]["clk_cycle"].as<unsigned>();

    clocks[name] =
        std::make_unique<sc_clock>(sc_gen_unique_name((name + "_clk").c_str()),
                                   sc_time(cycle_ns, SC_NS), 0.5);

    return *clocks.at(name);
  }
};

struct ChipletBase : sc_module {
protected:
  const std::string chiplet_name;
  const unsigned chiplet_id;

  ChipletClocks clocks;

public:
  ChipletBase(sc_module_name name, unsigned id, SystemConfig sysconf)
      : sc_module(name), chiplet_name(name), chiplet_id(id),
        clocks(sysconf.chiplets[chiplet_name].config) {}

  virtual ~ChipletBase() = default;

  std::unique_ptr<InterconnectBase> interconnect;
};