#include "modules/interconnects/Manager.h"
#include "modules/interconnects/Generic.h"
#include "modules/interconnects/serial_link/SerialLink.h"

#include "logging.h"

#include "common/System.h"

std::unique_ptr<InterconnectBase> InterconnectManager::create_interconnect() {
  switch (interconnect_config.type.type) {
  case InterconnectType::Type::PCIe:
  case InterconnectType::Type::UCIe:
    return std::make_unique<GenericInterconnect>(
        sc_module_name(interconnect_config.type.to_string().c_str()),
        chiplet_id, chiplet_config, interconnect_config, dma_engine);
  case InterconnectType::Type::SerialLink:
    return std::make_unique<SerialLink>(
        sc_module_name(interconnect_config.type.to_string().c_str()),
        chiplet_id, chiplet_config, interconnect_config);
  default:
    LOG_ERROR(interconnect_config.type.to_string() << " not implemented");
    return nullptr;
  }
}