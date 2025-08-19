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
        std::cout << "### Simultaneous Requests Example" << std::endl;
        const size_t num_bytes = 8;
        uint8_t *data = new uint8_t[num_bytes];

        for (size_t i = 0; i < num_bytes; ++i) {
          data[i] = static_cast<uint8_t>(i);
        }

        std::cout << "Core0 Buffer contents (" << num_bytes
                  << " bytes):" << std::endl;
        for (size_t i = 0; i < num_bytes; ++i) {
          std::cout << "data[" << i << "] = " << static_cast<unsigned>(data[i])
                    << std::endl;
        }

        auto h1 = core.send_request(TLM_WRITE_COMMAND, 1, 1, 0x1000, true, true,
                                    reinterpret_cast<unsigned char *>(data),
                                    num_bytes, 2, 4, 1);
        SC_LOG_DEBUG_NO_TX(&core, "Core0 can continue...");

        h1->wait();

        wait(400, SC_NS);

        std::cout << "### AXI Fixed Burst Example" << std::endl;

        auto h2 = core.send_request(TLM_READ_COMMAND, 2, 1, 0x1000, true, true,
                                    reinterpret_cast<unsigned char *>(data),
                                    num_bytes, 2, 4, 0);
        SC_LOG_DEBUG_NO_TX(&core, "Core0 can continue...");

        h2->wait();

        std::cout << "Core0 Buffer contents (" << num_bytes
                  << " bytes):" << std::endl;
        for (size_t i = 0; i < num_bytes; ++i) {
          std::cout << "data[" << i << "] = " << static_cast<unsigned>(data[i])
                    << std::endl;
        }

        std::cout << "### AXI Cache Read Miss Example" << std::endl;

        auto h3 = core.send_request(TLM_READ_COMMAND, 3, 1, 0x1000, true, false,
                                    reinterpret_cast<unsigned char *>(data),
                                    num_bytes, 2, 4, 0);
        SC_LOG_DEBUG_NO_TX(&core, "Core0 can continue...");

        h3->wait();

        std::cout << "### AXI Cache Read Hit Example" << std::endl;

        auto h4 = core.send_request(TLM_READ_COMMAND, 4, 1, 0x1000, true, false,
                                    reinterpret_cast<unsigned char *>(data),
                                    num_bytes, 2, 4, 0);
        SC_LOG_DEBUG_NO_TX(&core, "Core0 can continue...");

        h4->wait();
      },
      [](Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {}}},
    {{1, 1},
     {[](Core &core, UtilizationTracker *tracker) {
        const size_t num_bytes = 8;
        uint8_t *data = new uint8_t[num_bytes];

        auto h1 = core.send_request(TLM_READ_COMMAND, 11, 1, 0x1000, true, true,
                                    reinterpret_cast<unsigned char *>(data),
                                    num_bytes, 2, 4, 1);
        SC_LOG_DEBUG_NO_TX(&core, "Core1 can continue...");

        h1->wait();

        std::cout << "Core1 Buffer contents (" << num_bytes
                  << " bytes):" << std::endl;
        for (size_t i = 0; i < num_bytes; ++i) {
          std::cout << "data[" << i << "] = " << static_cast<unsigned>(data[i])
                    << std::endl;
        }

        delete h1;
      },
      [](Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {}}},
};