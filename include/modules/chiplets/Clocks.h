#pragma once

#include <systemc>
#include <yaml-cpp/yaml.h>

#include "logging.h"

using namespace sc_core;

struct Clocks {
    const YAML::Node config;

    std::map<std::string, std::unique_ptr<sc_clock>> clocks;

    Clocks(const YAML::Node &config) : config(config) {}

    sc_clock &get(const std::string &name = "") {
        if (auto it = clocks.find(name); it != clocks.end()) {
            return *it->second;
        }

        std::stringstream ss(name);
        std::string       token;
        YAML::Node        current = YAML::Clone(config);
        while (std::getline(ss, token, '.')) {
            if (!current[token]) {
                LOG_ERROR("Clock path does not exist: " << name);
            }
            current = current[token];
        }

        unsigned cycle_ns;
        if (current && current["clk_cycle"]) {
            cycle_ns = current["clk_cycle"].as<unsigned>();
        } else {
            LOG_ERROR("Clock path does not exist: " << name);
        }

        clocks[name] =
            std::make_unique<sc_clock>(sc_gen_unique_name((name + "_clk").c_str()), sc_time(cycle_ns, SC_NS), 0.5);

        return *clocks.at(name);
    }
};