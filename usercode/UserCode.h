#pragma once

#include "common/Tracker.h"

#include "include/configs.h"
#include "include/globals.h"
#include "include/logging.h"

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
        const size_t num_bytes = 16;
        uint8_t *data = new uint8_t[num_bytes];

        for (size_t i = 0; i < num_bytes; ++i) {
          data[i] = static_cast<uint8_t>(i);
        }

        auto h = core.send_request(TLM_WRITE_COMMAND, 1, 1, 0x1000, true, true,
                                   reinterpret_cast<unsigned char *>(data),
                                   num_bytes, 2, 8, 1);

        SC_LOG_DEBUG_NO_TX(&core, "WRITE OP - CACHE SKIP - CORE CONTINUE");

        h->wait();

        SC_LOG_DEBUG_NO_TX(&core, "WRITE OP - CACHE SKIP - DATA READY");

        h = core.send_request(TLM_READ_COMMAND, 2, 1, 0x1000, true, false,
                              reinterpret_cast<unsigned char *>(data),
                              num_bytes, 2, 8, 1);

        SC_LOG_DEBUG_NO_TX(&core, "READ OP - CACHE USE - CORE CONTINUE");

        h->wait();

        SC_LOG_DEBUG_NO_TX(&core, "READ OP - CACHE USE - DATA READY");

        std::cout << "Core1 Buffer contents (" << num_bytes
                  << " bytes):" << std::endl;
        for (size_t i = 0; i < num_bytes; ++i) {
          std::cout << "data[" << i << "] = " << static_cast<unsigned>(data[i])
                    << std::endl;
        }

        h = core.send_request(TLM_WRITE_COMMAND, 3, 1, 0x1000, true, false,
                              reinterpret_cast<unsigned char *>(data),
                              num_bytes, 2, 8, 1);

        SC_LOG_DEBUG_NO_TX(&core, "WRITE OP - CACHE USE - CORE CONTINUE");

        h->wait();

        SC_LOG_DEBUG_NO_TX(&core, "WRITE OP - CACHE USE - DATA READY");
      },
      [](Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {}}}};