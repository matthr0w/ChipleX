#pragma once

#include "common/System.h"

#include "modules/DMAEngine.h"
#include "modules/interconnects/Base.h"

class InterconnectManager {
public:
  InterconnectManager(unsigned chiplet_id, ChipletConfig chiplet_config,
                      InterconnectConfig interconnect_config,
                      DMAEngine *dma_engine)
      : chiplet_id(chiplet_id), chiplet_config(chiplet_config),
        interconnect_config(interconnect_config), dma_engine(dma_engine) {};

  std::unique_ptr<InterconnectBase> create_interconnect();

private:
  unsigned chiplet_id;
  ChipletConfig chiplet_config;
  InterconnectConfig interconnect_config;
  DMAEngine *dma_engine;
};