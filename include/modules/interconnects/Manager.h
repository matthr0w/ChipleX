#pragma once

#include "common/System.h"

#include "modules/DMAEngine.h"
#include "modules/interconnects/InterconnectBase.h"

class InterconnectManager {
public:
  InterconnectManager(unsigned chiplet_id, ChipletConfig chiplet_config,
                      InterconnectConfig interconnect_config,
                      unsigned num_cores, DMAEngine *dma_engine)
      : chiplet_id(chiplet_id), chiplet_config(chiplet_config),
        interconnect_config(interconnect_config), num_cores(num_cores),
        dma_engine(dma_engine) {};

  std::unique_ptr<InterconnectBase> create_interconnect();

private:
  const unsigned chiplet_id;
  const ChipletConfig chiplet_config;
  const InterconnectConfig interconnect_config;
  const unsigned num_cores;
  DMAEngine *dma_engine;
};