#pragma once

#include "modules/DMAEngine.h"
#include "modules/interconnects/Base.h"

std::unique_ptr<InterconnectBase>
create_interconnect(const std::string &type, unsigned chiplet_id,
                    unsigned axi_width, unsigned num_cores,
                    unsigned num_interconnects, DMAEngine &dma_engine);