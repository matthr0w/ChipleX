#pragma once

#include "modules/DMAEngine.h"
#include "modules/interconnects/InterconnectBase.h"
#include "setup/Types.h"

class InterconnectManager {
  public:
	InterconnectManager(unsigned chiplet_id, ChipletConfig chiplet_config)
	    : chiplet_id(chiplet_id), chiplet_config(chiplet_config) {};

	std::unique_ptr<InterconnectBase> create_interconnect(const std::string name, const unsigned interconnect_id,
	                                                      const InterconnectConfig interconnect_config,
	                                                      DMAEngine               *dma_engine = nullptr);

  private:
	const unsigned      chiplet_id;
	const ChipletConfig chiplet_config;
};