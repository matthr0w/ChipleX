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
          data[i] = static_cast<uint8_t>(i);
        }

        auto reqw = Core::WriteRequest(
                        1, reinterpret_cast<unsigned char *>(data), num_bytes)
                        .set_dest(3)
                        .set_addr(0x1000)
                        .skip_cache();

        auto h = core.write(reqw);

        auto reqr = Core::ReadRequest(1, 0x1000,
                                      reinterpret_cast<unsigned char *>(data),
                                      num_bytes)
                        .set_dest(3)
                        .skip_cache();

        h = core.read(reqr);
        h->wait();

        std::cout << "Data Buffer" << std::endl;
        for (size_t i = 0; i < num_bytes; ++i) {
          std::cout << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(data[i]);
          if ((i + 1) % 16 == 0)
            std::cout << "\n";
          else
            std::cout << " ";
        }

        delete[] data;

        sc_stop();
      },
      [](Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {}}}};