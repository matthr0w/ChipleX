#include "modules/interconnects/utils.h"
#include "modules/interconnects/Generic.h"
#include "modules/interconnects/serial_link/SerialLink.h"

std::unique_ptr<InterconnectBase>
create_interconnect(const std::string &type, unsigned chiplet_id,
                    unsigned axi_width, unsigned num_cores,
                    unsigned num_interconnects, DMAEngine &dma_engine) {
  const Config &config = ConfigRegistry::instance().get("Interconnect");
  if (type == "Custom" || type == "PCIe" || type == "UCIe") {
    return std::make_unique<GenericInterconnect>(
        sc_module_name(type.c_str()), chiplet_id, axi_width, num_cores,
        num_interconnects,
        config.get<unsigned>("interconnect_protocol.flit_size"),
        config.get<unsigned>("interconnect_protocol.overhead_size"),
        config.get<unsigned>("interconnect.staging_buffer_size"),
        config.get<unsigned>("interconnect.link_buffer_size"),
        config.get<double>("interconnect.bandwidth_chiplets"),
        chiplet_distance_um, &dma_engine);
  } else if (type == "SerialLink") {
    return std::make_unique<SerialLink>(
        sc_module_name(type.c_str()), chiplet_id, axi_width, num_cores,
        num_interconnects, config.get<unsigned>("num_credits"));
  }
  throw std::runtime_error(type + " not implemented");
}