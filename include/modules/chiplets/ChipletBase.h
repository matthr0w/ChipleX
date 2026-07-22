#pragma once

#include <systemc>

#include "modules/chiplets/ChipletDescriptor.h"
#include "modules/chiplets/ChipletRegistry.h"
#include "modules/chiplets/Clocks.h"
#include "modules/extensions/ExtensionLayer.h"
#include "modules/interconnects/InterconnectBase.h"
#include "setup/Types.h"

using namespace sc_core;

struct ChipletBase : sc_module {
  protected:
	const unsigned          chiplet_id;
	const std::string       chiplet_name;
	const ChipletConfig     chiplet_config;
	const ChipletDescriptor chiplet_desc;

	// Clock databases
	Clocks                        chiplet_clocks;
	std::map<std::string, Clocks> accel_clocks;
	std::map<std::string, Clocks> interconnect_clocks;

  public:
	ChipletBase(sc_module_name name, unsigned id, ChipletConfig chiplet_config, const ChipletDescriptor &chiplet_desc)
	    : sc_module(name),
	      chiplet_id(id),
	      chiplet_name(std::string(name)),
	      chiplet_config(chiplet_config),
	      chiplet_desc(chiplet_desc),
	      chiplet_clocks(chiplet_config.node) {
		ChipletRegistry::instance().register_chiplet(chiplet_id, chiplet_name, chiplet_desc);
		for (const auto &[name, config] : chiplet_config.accels) {
			accel_clocks.emplace(name, Clocks(config.node));
		}
		for (const auto &[name, config] : chiplet_config.interconnects) {
			interconnect_clocks.emplace(name, Clocks(config.node));
		}
	}

	virtual ~ChipletBase() = default;

	std::map<std::string, std::unique_ptr<ExtensionLayer>>   ext_layers;
	std::map<std::string, std::unique_ptr<InterconnectBase>> interconnects;

  protected:
	Clocks &get_accel_clocks(const std::string &accel_name) {
		auto it = accel_clocks.find(accel_name);
		if (it != accel_clocks.end()) {
			return it->second;
		}
		return chiplet_clocks;
	}

	Clocks &get_interconnect_clocks(const std::string &interconnect_name) {
		auto it = interconnect_clocks.find(interconnect_name);
		if (it != interconnect_clocks.end()) {
			return it->second;
		}
		return chiplet_clocks;
	}
};