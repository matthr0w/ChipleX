#pragma once

#include "configs.h"
#include "globals.h"
#include "logging.h"

#include "common/Tracker.h"

#include "modules/Core.h"

using CoreFunctions =
    std::pair<std::function<void(Core &, UtilizationTracker *)>, // main thread
              std::function<void(Core &, UtilizationTracker *,
                                 tlm_generic_payload *)> // interrupt handler
              >;
using CoreKey = std::pair<int, int>;

inline std::map<CoreKey, CoreFunctions> core_code = {
    // FPGA Core0
    {{0, 0},
     {[](Core &core, UtilizationTracker *tracker) {},
      [](Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {}}},
    // Chiplet1 Core0
    {{1, 0},
     {[](Core &core, UtilizationTracker *tracker) {
        size_t num_bytes = 64;
        uint8_t *data = new uint8_t[num_bytes];

        for (size_t i = 0; i < num_bytes; ++i) {
          data[i] = static_cast<uint8_t>(10);
        }

        auto h =
            core.write(1, 2, 0x1000, reinterpret_cast<unsigned char *>(data),
                       num_bytes, false);
        h->wait();

        h = core.read(2, 2, 0x1000, reinterpret_cast<unsigned char *>(data),
                      num_bytes, false);
        h->wait();

        h = core.write(1, 2, 0x1000, reinterpret_cast<unsigned char *>(data),
                       num_bytes, false);
        h->wait();
      },
      [](Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {}}}};