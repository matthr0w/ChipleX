#pragma once

#include <functional>
#include <map>
#include <utility>

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

// =========================================================================
// This file allows you to program the FPGA and chiplet cores.
//
// Each core supports two user-defined functions:
//   - Main thread function (runs once at simulation start; you may use an
//     infinite loop with wait() if it should remain active)
//   - Interrupt handler (called when the core receives an IRQ)
//
// If no user code is provided for a core, it will remain idle.
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
//
//    Parameters:
//    - `write_prob`: Probability of issuing a write
//       (0.0 = all reads, 1.0 = all writes)
//    - `destination_min` and `destination_max`: Target modules ID range
//       (0 = FPGA, 1 = Chiplet1, ...)
//    - `data_size`: Number of bytes per request
//
// 2. ChipletPayload* send_request(tlm_command command,
//                                 int request_id,
//                                 int destination_id,
//                                 uint32_t address,
//                                 bool fixed_address,
//                                 bool is_volatile,
//                                 unsigned char* data,
//                                 unsigned int data_size);
//
//    Sends a TLM request to the target over the bus.
//
//    Parameters:
//    - `command`: `TLM_READ_COMMAND` or `TLM_WRITE_COMMAND`
//    - `request_id`: Used to identify the request later in the interrupt
//       handler (you may start at 0 and increment as needed).
//    - `destination_id`: Target module ID
//       (0 = FPGA, 1 = Chiplet1, ...)
//    - `address`: The address to read from or write to.
//       - `TLM_READ_COMMAND`: the passed address is always used.
//       - `TLM_WRITE_COMMAND`: if `fixed_address` is true, the passed
//          address is used; otherwise, the memory controller will assign the
//          address and you can pass any address.
//    - `fixed_address`: Indicates whether the write request should use the
//       provided address (`true`) or allow the memory controller to allocate
//       it dynamically (`false`). Ignored for read requests.
//    - `is_volatile`: Indicates whether the data should bypass the cache.
//       If set to `true`, the cache will be skipped during access. This is
//       useful for frequently changing or time-sensitive data that should
//       always be read directly from memory.
//    - `data`: Must be allocated on the heap using `new`.
//       - `TLM_WRITE_COMMAND`: the buffer contents will be sent to the target.
//       - `TLM_READ_COMMAND`: an empty buffer of the appropriate size must
//         be passed. DO NOT delete the buffer manually. It will be freed
//         automatically when the returned transaction is deleted.
//    - `data_size`: Number of bytes in the request buffer
//
//    Returns:
//      A pointer to the `ChipletPayload` transaction that was internally set up
//      by this function. You are responsible for deleting the returned
//      transaction using `delete`. This will also correctly deallocate the
//      associated data buffer.
//
//    Notes:
//    - The returned transaction's contents (e.g., data pointer) are only valid
//      and meaningful for on-chip read and write requests.
//    - For off-chip requests (to other chiplets or the FPGA), the response
//      transaction does NOT contain meaningful data and can be ignored.
//    - For off-chip read requests: the initiating module will receive an
//      IRQ when the data becomes available and should handle the data fetch
//      in the IRQ handler.
//    - For off-chip write requests: the target will receive an IRQ when the
//      write has completed and should handle the data fetch in the IRQ handler.
//
// -----------------------------
//  Simulation Notes:
// -----------------------------
//
// - Your user functions receive a pointer to the utilization tracker:
//     void main_thread(Module &module, UtilizationTracker *tracker)
//     void irq_handler(Module &module, UtilizationTracker *tracker,
//                      tlm_generic_payload *transaction)
//
// - You should use `tracker.set_active()` and `tracker.set_idle()` to track the
//   utilization.
//
// - You should add realistic process delays before sending requests.
//
// -----------------------------
//  Interrupt Handler Notes:
// -----------------------------
//
// - Your handler receives a pointer to the incoming transaction:
//     void irq_handler(Module &module, UtilizationTracker *tracker,
//                      tlm_generic_payload *transaction)
//
// - The incoming transaction does NOT contain any valid payload data.
//   It only includes important metadata such as:
//     - `get_address()`: the location where the data can be fetched
//     - `get_data_length()`: the size of the data
//     - `ChipletExtension`: custom metadata like `request_id`, etc.
//
// - To fetch the actual data related to this IRQ, you must issue a new
//   on-chip request using the `send_request()` function, passing the
//   parameters from the IRQ transaction.
//
// - You are responsible for deleting the response returned from
//   `send_request()` to avoid memory leaks.
//
// - DO NOT delete the IRQ transaction passed to the handler.
//   It is owned and managed by the system that dispatched the IRQ.
//
// -----------------------------
//  Configuration Access:
// -----------------------------
//
// - Global constants:                 see `globals.h`
// - FPGA configuration parameters:
//     const Config &config = ConfigRegistry::instance().get("FPGA");
//     type param = config.get<type>("YAML.PATH")
// - Chiplet configuration parameters:
//     const Config &config = ConfigRegistry::instance().get("Chiplet");
//     type param = config.get<type>("YAML.PATH")
// - Interconnect configuration parameters:
//     const Config &config = ConfigRegistry::instance().get("Interconnect");
//     type param = config.get<type>("YAML.PATH")
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
// Use the following format to define behavior per core in the `core_code`:
//
//     // FPGA Core0
//     {{0, 0},
//      {[](Core &core, UtilizationTracker *tracker) {
//           // MAIN THREAD CODE
//       },
//       [](Core &core, UtilizationTracker *tracker,
//          tlm_generic_payload *transaction) {
//           // INTERRUPT HANDLER CODE
//       }}},
//
//     // ChipletX CoreY
//     {{X, Y},
//      {[](Core &core, UtilizationTracker *tracker) {
//           // MAIN THREAD CODE
//       },
//       [](Core &core, UtilizationTracker *tracker,
//          tlm_generic_payload *transaction) {
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
//  {[](Core &core, UtilizationTracker *tracker) {
//     SC_LOG_DEBUG_NO_TX(&core, "Starting Core Logic");
//     tracker.set_active();
//     uint32_t *data = new uint32_t(0xABCD);
//     wait(100, SC_NS); // example delay
//     auto response =
//         core.send_request(TLM_WRITE_COMMAND, 0, 0, 0x1000, false,
//                           reinterpret_cast<unsigned char *>(data), 4);
//     delete response;
//     tracker.set_idle();
//   },
//   [](Core &core, UtilizationTracker *tracker,
//      tlm_generic_payload *transaction) {
//     auto *ext = transaction->get_extension<ChipletExtension>();
//     if (ext) {
//       SC_LOG_DEBUG_NO_TX(&core,
//                          "Received IRQ to request ID " << ext->request_id);
//     }
//   }}},
//
// `send_random` Examples:
// // Send 64 bytes every 200ns randomly to modules 0–2
// // (FPGA, Chiplet1, Chiplet2) with 50% write probability:
// module.send_random(200, 0.5, 0, 2, 64);
//
// // Send 32 bytes every 100ns to Chiplet1 with 75% write probability:
// module.send_random(100, 0.75, 1, 1, 32);
//
// ============================================================

static void matrix_multiply(Core &core, uint32_t src_addr1,
                            uint32_t src_addr2, uint32_t dest_addr) {
  unsigned char *buf1 = new unsigned char[4 * sizeof(int)];
  unsigned char *buf2 = new unsigned char[4 * sizeof(int)];

  auto *rx1 = core.send_request(TLM_READ_COMMAND, 1, 1, src_addr1, true, true,
                                buf1, 4 * sizeof(int));
  // only initial matrix should be stored in cache
  auto *rx2 = core.send_request(TLM_READ_COMMAND, 2, 1, src_addr2, true, false,
                                buf2, 4 * sizeof(int));

  int *A = reinterpret_cast<int *>(rx1->get_data_ptr());
  int *B = reinterpret_cast<int *>(rx2->get_data_ptr());
  int result[4];

  result[0] = A[0] * B[0] + A[1] * B[2];
  result[1] = A[0] * B[1] + A[1] * B[3];
  result[2] = A[2] * B[0] + A[3] * B[2];
  result[3] = A[2] * B[1] + A[3] * B[3];

  auto *data = new unsigned char[4 * sizeof(int)];
  memcpy(data, result, 4 * sizeof(int));
  auto *tx = core.send_request(TLM_WRITE_COMMAND, 3, 1, dest_addr, true, true,
                               data, 4 * sizeof(int));
  delete tx;

  delete rx1;
  delete rx2;
}

inline std::map<CoreKey, CoreFunctions> core_code = {
    // FPGA Core0
    {{0, 0},
     {[](Core &core, UtilizationTracker *tracker) {
        tracker->set_active();

        // |1 2|
        // |3 4|
        int matrix[4] = {1, 2, 3, 4};
        auto *data = new unsigned char[sizeof(matrix)];
        memcpy(data, matrix, sizeof(matrix));

        auto *tx = core.send_request(TLM_WRITE_COMMAND, 0, 1, 0x0, false, true,
                                     data, sizeof(matrix));
        delete tx;

        tracker->set_idle();
      },
      [](Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {
        tracker->set_active();

        auto *data = new unsigned char[4 * sizeof(int)];
        auto *rx = core.send_request(TLM_READ_COMMAND, 1, 0,
                                     transaction->get_address(), true, true,
                                     data, 4 * sizeof(int));
        int *result = reinterpret_cast<int *>(data);

        std::cout << "A^6 = [" << result[0] << " " << result[1] << " ; "
                  << result[2] << " " << result[3] << "]" << std::endl;

        tracker->set_idle();
      }}},
    // Chiplet1 Core0
    {{1, 0},
     {[](Core &core, UtilizationTracker *tracker) {},
      [](Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {
        static const Config &config = ConfigRegistry::instance().get("Chiplet");

        tracker->set_active();

        uint32_t matrix_addr = transaction->get_address();

        matrix_multiply(core, matrix_addr, matrix_addr, 0x1000); // A^2 = A * A
        // cycle count simulated with Spike
        wait(58 * config.get<sc_time>("cores.clk_cycle"));
        matrix_multiply(core, 0x1000, matrix_addr, 0x2000); // A^3 = A^2 * A
        // cycle count simulated with Spike
        wait(58 * config.get<sc_time>("cores.clk_cycle"));
        matrix_multiply(core, 0x2000, matrix_addr, 0x3000); // A^4 = A^3 * A
        // cycle count simulated with Spike
        wait(58 * config.get<sc_time>("cores.clk_cycle"));
        matrix_multiply(core, 0x3000, matrix_addr, 0x4000); // A^5 = A^4 * A
        // cycle count simulated with Spike
        wait(58 * config.get<sc_time>("cores.clk_cycle"));
        matrix_multiply(core, 0x4000, matrix_addr, 0x5000); // A^6 = A^5 * A
        // cycle count simulated with Spike
        wait(58 * config.get<sc_time>("cores.clk_cycle"));

        auto *data = new unsigned char[4 * sizeof(int)];
        auto *rx = core.send_request(TLM_READ_COMMAND, 4, 1, 0x5000, true, true,
                                     data, 4 * sizeof(int));

        auto *result = new unsigned char[4 * sizeof(int)];
        memcpy(result, data, 4 * sizeof(int));

        delete rx;

        auto *tx = core.send_request(TLM_WRITE_COMMAND, 5, 0, 0x0, false, true,
                                     result, 4 * sizeof(int));

        delete tx;

        tracker->set_idle();
      }}},
};