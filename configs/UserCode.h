#pragma once

#include <functional>
#include <map>
#include <utility>

#include "chiplet/Config.h"
#include "chiplet/Core.h"
#include "fpga/Config.h"
#include "fpga/Generator.h"

#include "include/globals.h"
#include "include/logging.h"

using GeneratorFunctions =
    std::pair<std::function<void(fpga::Generator &)>, // main thread
              std::function<void(fpga::Generator &,
                                 tlm_generic_payload *)> // interrupt handler
              >;

using CoreFunctions =
    std::pair<std::function<void(chiplet::Core &)>, // main thread
              std::function<void(chiplet::Core &,
                                 tlm_generic_payload *)> // interrupt handler
              >;
using CoreKey = std::pair<int, int>;

// DO NOT EDIT CODE ABOVE THIS LINE

// =========================================================================
// This file allows you to program the FPGA generator and the chiplet cores.
//
// Each module supports two user-defined functions:
//   - Main thread function (runs once at simulation start; you may use an
//     infinite loop with wait() if it should remain active)
//   - Interrupt handler (called when the module receives an IRQ)
//
// If no user code is provided for a module, it will remain idle.
// =========================================================================
//
// -----------------------------
//  Available Functions:
// -----------------------------
//
// 1. void send_random(unsigned int delay,
//                     double write_prob,
//                     unsigned int destination_min,
//                     unsigned int destination_max,
//                     size_t data_size);
//
//    Sends random read/write requests every `delay` nanoseconds.
//    - `write_prob`: Probability of issuing a write
//       (0.0 = all reads, 1.0 = all writes)
//    - `destination_min` and `destination_max`: Target modules ID range
//       (0 = FPGA, 1 = Chiplet1, ...)
//    - `data_size`: Number of bytes per request
//
// 2. void send_request(tlm_command command,
//                      int request_id,
//                      int destination_id,
//                      uint32_t address,
//                      unsigned char* data,
//                      unsigned int data_size);
//
//    Sends a single read/write request over the bus.
//    - `request_id`: Used to identify the request later in the interrupt
//    handler (you may start at 0 and increment as needed)
//    - `destination_id`: Target module ID
//       (0 = FPGA, 1 = Chiplet1, ...)
//    - `data`: Must be allocated on the heap (`new`).
//       DO NOT delete it manually. Memory will be cleaned up by the system.
//    - `data_size`: Number of bytes of the request
//
//    Note:
//    `send_request` is blocking and only returns when the transaction is
//    complete. Add appropriate `wait()` calls before sending to simulate
//    realistic processing delays.
//
// -----------------------------
//  Interrupt Handler Notes:
// -----------------------------
//
// - Your handler receives a pointer to the incoming transaction:
//     void irq_handler(Module &module, tlm_generic_payload *transaction)
// - Use `transaction->get_extension<ChipletExtension>()` to access metadata
//   (e.g., `request_id`).
//
// -----------------------------
//  Configuration Access:
// -----------------------------
//
// - Global constants:                 see `globals.h`
// - FPGA configuration parameters:    `fpga::Config::instance().PARAMETER()`
// - Chiplet configuration parameters: `chiplet::Config::instance().PARAMETER()`
//
// -----------------------------
//  Logging:
// -----------------------------
//
// - Use `SC_LOG_DEBUG_NO_TX(&module, "message")` to print to the unified log.
//
// -----------------------------
//  Code Instructions:
// -----------------------------
//
// FPGA Generator:
// Implement your logic inside the blocks highlighted in the code below.
//
// Chiplet Cores:
// Use the following format to define behavior per chiplet/core in the
// `core_code`:
//
//     // ChipletX CoreY
//     {{X, Y},
//      {[](chiplet::Core &core) {
//           // MAIN THREAD CODE
//       },
//       [](chiplet::Core &core, tlm_generic_payload *transaction) {
//           // INTERRUPT HANDLER CODE
//       }}},
//
// -----------------------------
//  Examples:
// -----------------------------
//
// Core Code Example:
// // Chiplet1 Core0
// {{1, 0},
//  {[](chiplet::Core &core) {
//     SC_LOG_DEBUG_NO_TX(&core, "Starting Core Logic");
//     uint32_t *data = new uint32_t(0xABCD);
//     core.send_request(TLM_WRITE_COMMAND, 0, 0, 0x1000,
//                       reinterpret_cast<unsigned char *>(data), 4);
//   },
//   [](chiplet::Core &core, tlm_generic_payload *transaction) {
//     auto *ext = transaction->get_extension<ChipletExtension>();
//     if (ext) {
//       SC_LOG_DEBUG_NO_TX(&core,
//                          "Received IRQ to request ID " << ext->request_id);
//     }
//   }}},
//
// `send_request` Examples:
// // Send 64 bytes every 200ns randomly to modules 0–2
// // (FPGA, Chiplet1, Chiplet2) with 50% write probability:
// module.send_random(200, 0.5, 0, 2, 64);
//
// // Send 32 bytes every 100ns to Chiplet1 with 75% write probability:
// module.send_random(100, 0.75, 1, 1, 32);
//
// ============================================================

inline GeneratorFunctions generator_code = {
    [](fpga::Generator &gen) {
      // FPGA GENERATOR CODE BELOW
      SC_LOG_DEBUG_NO_TX(&gen, "Hello from FPGA Generator!");
      // ------------------------------
    },
    [](fpga::Generator &gen, tlm::tlm_generic_payload *transaction) {
      // FPGA INTERRUPT HANDLER CODE BELOW
      SC_LOG_DEBUG_NO_TX(&gen, "Hello from FPGA Interrupt Handler!");
      // ------------------------------
    }};

inline std::map<CoreKey, CoreFunctions> core_code = {
    // Chiplet1 Core0
    {{1, 0},
     {[](chiplet::Core &core) {
        SC_LOG_DEBUG_NO_TX(&core, "Hello from Chiplet1 Core0!");
        core.send_random(200, 0.5, 0, 2, 64);
      },
      [](chiplet::Core &core, tlm_generic_payload *transaction) {
        SC_LOG_DEBUG_NO_TX(&core,
                           "Hello from Chiplet1 Core0 Interrupt Handler!");
      }}},
};