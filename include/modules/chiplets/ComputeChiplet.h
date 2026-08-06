#pragma once

#include <yaml-cpp/yaml.h>

#include "logging.h"

#include "modules/Bus.h"
#include "modules/chiplets/ChipletBase.h"
#include "modules/chiplets/ChipletDescriptor.h"
#include "modules/Core.h"
#include "modules/DMAEngine.h"
#include "modules/HWAccel.h"
#include "modules/interconnects/InterconnectBase.h"
#include "modules/interconnects/Manager.h"
#include "modules/Memory.h"
#include "setup/Types.h"

struct ComputeChiplet : ChipletBase {
	static ChipletDescriptor build_descriptor(std::string name, unsigned id, ChipletConfig chiplet_config) {
		ChipletDescriptor desc;
		desc.chiplet_id   = id;
		desc.chiplet_name = name;

		// Cores
		unsigned num_cores = chiplet_config.node["cores"]["num"].as<unsigned>();
		for (unsigned i = 0; i < num_cores; i++) {
			desc.add_module(CORE_MODULE_NAME + std::to_string(i), {AXIModuleType::BUS_MANAGER});
		}

		// Accelerators
		for (auto &[name, config] : chiplet_config.accels) {
			desc.add_module(name, {AXIModuleType::BUS_SUBORDINATE});
		}

		// DMA Engine
		desc.add_module(DMA_ENGINE_MODULE_NAME, {AXIModuleType::BUS_MANAGER, AXIModuleType::BUS_SUBORDINATE});

		// Memory
		desc.add_module(MEMORY_MODULE_NAME, {AXIModuleType::BUS_SUBORDINATE});

		// Bus
		desc.add_module(BUS_MODULE_NAME, {AXIModuleType::BUS});

		// Extension Layers + Interconnects
		for (auto &[name, config] : chiplet_config.interconnects) {
			const std::string ext_name = EXT_LAYER_MODULE_NAME + "_" + name;
			desc.add_module(ext_name, {AXIModuleType::BUS_MANAGER, AXIModuleType::BUS_SUBORDINATE});
			desc.add_module(name, {AXIModuleType::INTERCONNECT, AXIModuleType::MANAGER, AXIModuleType::SUBORDINATE});
		}

		// Generate LUTs
		desc.generate_luts();

		return desc;
	}

	// Cores
	const unsigned                     num_cores;
	std::vector<std::unique_ptr<Core>> cores;

	// Accelerators
	std::map<std::string, std::unique_ptr<HWAccel>> accels;

	// DMA Engine
	DMAEngine dma_engine;

	// Memory
	Memory memory;

	// Bus
	Bus bus;

	ComputeChiplet(sc_module_name name, unsigned id, ChipletConfig chiplet_config, CyclesDB cycles)
	    : ChipletBase(name, id, chiplet_config, build_descriptor(std::string(name), id, chiplet_config)),
	      num_cores(chiplet_config.node["cores"]["num"].as<unsigned>()),
	      memory(MEMORY_MODULE_NAME.c_str(), chiplet_config),
	      dma_engine(DMA_ENGINE_MODULE_NAME.c_str(), chiplet_config),
	      bus(BUS_MODULE_NAME.c_str(), chiplet_id, chiplet_config, chiplet_desc.num_bus_managers(),
	          chiplet_desc.num_bus_subordinates()) {
		// Assertions
		LOG_ASSERT(chiplet_config.node["axi"]["width"].as<unsigned>() >= 8 ||
		               (chiplet_config.node["axi"]["width"].as<unsigned>() % 8) == 0,
		           "Parameter Error: AXI size must be a multiple of 8");

		// Cores
		for (unsigned i = 0; i < num_cores; ++i) {
			std::string core_name = CORE_MODULE_NAME + std::to_string(i);
			unsigned    num_irqs =
			    chiplet_desc.num_interconnects() * 2 + 1; // Interconnects + Extension Layers + DMA Engine
			cores.push_back(std::make_unique<Core>(core_name.c_str(), chiplet_name, chiplet_id, core_name, i,
			                                       chiplet_config, cycles, num_irqs));

			// Bind the core's clock and manager socket directly to the bus.
			cores[i]->clk.bind(chiplet_clocks.get("cores"));
			cores[i]->isocket.bind(*bus.managers[chiplet_desc.get_mgr_port(core_name)]);

			// Assign program code
			auto it = chiplet_config.module_code.find(core_name);
			if (it == chiplet_config.module_code.end()) {
				LOG_WARN(chiplet_name << "." << core_name << " has no code assigned. Staying idle.");
				continue;
			}
			std::visit(
			    [&](auto &code) {
				    using T = std::decay_t<decltype(code)>;
				    if constexpr (std::is_same_v<T, CPUCode>) {
					    // Assign CPU functions
					    cores[i]->main_fn      = code.main;
					    cores[i]->interrupt_fn = code.irq;
				    }
			    },
			    it->second);
		}

		// Accelerators
		for (const auto &[name, config] : chiplet_config.accels) {
			HWAccel &accel = *accels
			                      .emplace(name, std::make_unique<HWAccel>(name.c_str(), chiplet_name, chiplet_id, name,
			                                                               chiplet_config, cycles, &dma_engine))
			                      .first->second;

			// Bind clocks and sockets
			accel.clk.bind(get_accel_clocks(name).get());
			accel.tsocket.bind(*bus.subordinates[chiplet_desc.get_sub_port(name)]);

			// Assign program code
			auto it = chiplet_config.module_code.find(name);
			if (it == chiplet_config.module_code.end()) {
				LOG_WARN(chiplet_name << "." << name << " has no code assigned. Staying idle.");
				continue;
			}
			std::visit(
			    [&](auto &code) {
				    using T = std::decay_t<decltype(code)>;
				    if constexpr (std::is_same_v<T, AccelCode>) {
					    // Assign accelerator functions
					    accel.main_fn = code.main;
				    }
			    },
			    it->second);
		}

		// DMA Engine
		dma_engine.clk.bind(chiplet_clocks.get(DMA_ENGINE_MODULE_NAME));
		dma_engine.isocket.bind(*bus.managers[chiplet_desc.get_mgr_port(DMA_ENGINE_MODULE_NAME)]);
		dma_engine.tsocket.bind(*bus.subordinates[chiplet_desc.get_sub_port(DMA_ENGINE_MODULE_NAME)]);

		// Memory
		memory.clk.bind(chiplet_clocks.get(MEMORY_MODULE_NAME));
		memory.tsocket.bind(*bus.subordinates[chiplet_desc.get_sub_port(MEMORY_MODULE_NAME)]);

		// Bus
		bus.clk.bind(chiplet_clocks.get("axi"));

		// Interconnects
		InterconnectManager manager(chiplet_id, chiplet_config);
		for (const auto &[name, config] : chiplet_config.interconnects) {
			const unsigned    id       = chiplet_config.interconnect_ids.find(name)->second;
			const std::string ext_name = EXT_LAYER_MODULE_NAME + "_" + name;

			DMAEngine *dma_engine_ptr =
			    (config.node["use_dma"] && !config.node["use_dma"].as<bool>()) ? nullptr : &dma_engine;

			// Create extension layer
			auto ext_layer = std::make_unique<ExtensionLayer>(ext_name.c_str(), chiplet_config, dma_engine_ptr);
			ext_layer->clk.bind(chiplet_clocks.get("extensions"));

			// Create interconnect
			auto interconnect = manager.create_interconnect(name, id, config, dma_engine_ptr);
			interconnect->bind_clocks(get_interconnect_clocks(name));

			// Connect bus <-> extension layer <-> interconnect
			ext_layer->axi_out_up.bind(*bus.managers[chiplet_desc.get_mgr_port(ext_name)]);
			ext_layer->axi_in_up.bind(*bus.subordinates[chiplet_desc.get_sub_port(ext_name)]);
			ext_layer->axi_out_down.bind(*interconnect->axi_in_port);
			ext_layer->axi_in_down.bind(*interconnect->axi_out_port);

			// Move into base class
			ext_layers.emplace(ext_name, std::move(ext_layer));
			interconnects.emplace(name, std::move(interconnect));
		}

		// Interrupt lines
		// DMA Engine
		for (unsigned i = 0; i < num_cores; ++i) {
			dma_engine.irq_sockets[i].bind(cores[i]->irq_sockets[0]);
		}
		// Interconnects
		unsigned irq_idx = 1;
		for (const auto &[name, config] : chiplet_config.interconnects) {
			const std::string ext_name = EXT_LAYER_MODULE_NAME + "_" + name;
			for (unsigned i = 0; i < num_cores; ++i) {
				// Bind extension layer IRQ socket first
				ext_layers[ext_name]->irq_sockets[i].bind(cores[i]->irq_sockets[irq_idx]);
			}
			irq_idx++;
			for (unsigned i = 0; i < num_cores; ++i) {
				// Then bind corresponding interconnect IRQ socket
				interconnects[name]->irq_ports[i]->bind(cores[i]->irq_sockets[irq_idx]);
			}
			irq_idx++;
		}
	}
};