#include "logging.h"

#include "modules/interconnects/generic/Generic.h"
#include "modules/interconnects/Manager.h"
#include "modules/interconnects/serial_link/SerialLink.h"
#include "modules/interconnects/SPI.h"
#include "setup/Types.h"

std::unique_ptr<InterconnectBase> InterconnectManager::create_interconnect(const std::string        name,
                                                                           const unsigned           interconnect_id,
                                                                           const InterconnectConfig interconnect_config,
                                                                           DMAEngine               *dma_engine) {
	switch (interconnect_config.type.value) {
	case InterconnectType::Type::PCIe:
	case InterconnectType::Type::UCIe:
		return std::make_unique<GenericInterconnect>(sc_module_name(name.c_str()), chiplet_id, chiplet_config,
		                                             interconnect_id, interconnect_config, dma_engine);
	case InterconnectType::Type::SerialLink:
		return std::make_unique<SerialLink>(sc_module_name(name.c_str()), chiplet_id, chiplet_config, interconnect_id,
		                                    interconnect_config);
	case InterconnectType::Type::SPI:
		return std::make_unique<SPI>(sc_module_name(name.c_str()), chiplet_id, chiplet_config, interconnect_id,
		                             interconnect_config, dma_engine);
	default:
		LOG_ERROR(interconnect_config.type.to_string() << " not implemented");
		return nullptr;
	}
}