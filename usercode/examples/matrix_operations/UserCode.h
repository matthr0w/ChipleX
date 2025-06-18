#pragma once

#include <functional>
#include <map>
#include <utility>

#include "chiplet/Core.h"
#include "fpga/Generator.h"

#include "common/Tracker.h"

#include "include/configs.h"
#include "include/globals.h"
#include "include/logging.h"

using GeneratorFunctions = std::pair<
    std::function<void(fpga::Generator &, UtilizationTracker *)>, // main thread
    std::function<void(fpga::Generator &, UtilizationTracker *,
                       tlm_generic_payload *)> // interrupt handler
    >;

using CoreFunctions = std::pair<
    std::function<void(chiplet::Core &, UtilizationTracker *)>, // main thread
    std::function<void(chiplet::Core &, UtilizationTracker *,
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
//  {[](chiplet::Core &core, UtilizationTracker *tracker) {
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
//   [](chiplet::Core &core, UtilizationTracker *tracker,
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

struct MatrixHeader {
  uint32_t rows;
  uint32_t cols;
};

void inline print_matrix(const int *matrix, int rows, int cols,
                         const std::string &label = "") {
  if (!label.empty()) {
    std::cout << label << std::endl;
  }

  for (int i = 0; i < rows; ++i) {
    std::cout << "[ ";
    for (int j = 0; j < cols; ++j) {
      std::cout << matrix[i * cols + j] << " ";
    }
    std::cout << "]" << std::endl;
  }
}

inline GeneratorFunctions generator_code = {
    [](fpga::Generator &gen, UtilizationTracker *tracker) {
      // FPGA GENERATOR CODE BELOW
      const unsigned int MATRIX_ROWS = 10;
      const unsigned int MATRIX_COLS = 10;

      tracker->set_active();

      size_t header_size = sizeof(MatrixHeader);
      size_t matrix_size = MATRIX_ROWS * MATRIX_COLS * sizeof(int);
      size_t buffer_size = header_size + matrix_size;

      auto *data = new unsigned char[buffer_size];

      MatrixHeader *header = reinterpret_cast<MatrixHeader *>(data);
      header->rows = MATRIX_ROWS;
      header->cols = MATRIX_COLS;

      int *matrix = reinterpret_cast<int *>(data + header_size);
      for (int i = 0; i < MATRIX_ROWS * MATRIX_COLS; ++i) {
        matrix[i] = i + 1;
      }

      print_matrix(matrix, MATRIX_ROWS, MATRIX_COLS, "Input");

      auto *response = gen.send_request(TLM_WRITE_COMMAND, 0, 1, 0x0, false,
                                        true, data, buffer_size);

      delete response;

      tracker->set_idle();
      // ------------------------------
    },
    [](fpga::Generator &gen, UtilizationTracker *tracker,
       tlm_generic_payload *transaction) {
      // FPGA INTERRUPT HANDLER CODE BELOW
      tracker->set_active();

      auto addr = transaction->get_address();
      auto len = transaction->get_data_length();

      // read from FPGA RAM
      auto *response = gen.send_request(TLM_READ_COMMAND, 1, 0, addr, true,
                                        true, new unsigned char[len], len);

      // matrix header
      size_t header_size = sizeof(MatrixHeader);
      MatrixHeader *header =
          reinterpret_cast<MatrixHeader *>(response->get_data_ptr());
      uint32_t rows = header->rows;
      uint32_t cols = header->cols;

      // matrix
      int *matrix =
          reinterpret_cast<int *>(response->get_data_ptr() + header_size);

      print_matrix(matrix, rows, cols, "Output");

      delete response;

      tracker->set_idle();
      // ------------------------------
    }};

inline std::map<CoreKey, CoreFunctions> core_code = {
    // Chiplet1 Core0
    {{1, 0},
     {[](chiplet::Core &core, UtilizationTracker *tracker) {},
      [](chiplet::Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {
        static const Config &config = ConfigRegistry::instance().get("Chiplet");

        tracker->set_active();

        auto addr = transaction->get_address();
        auto len = transaction->get_data_length();

        // read from Chiplet1 RAM
        auto *response = core.send_request(TLM_READ_COMMAND, 0, 1, addr, true,
                                           true, new unsigned char[len], len);

        // matrix header
        size_t header_size = sizeof(MatrixHeader);
        MatrixHeader *header =
            reinterpret_cast<MatrixHeader *>(response->get_data_ptr());
        uint32_t rows = header->rows;
        uint32_t cols = header->cols;

        // matrix
        int *matrix =
            reinterpret_cast<int *>(response->get_data_ptr() + header_size);

        // add +1
        for (int i = 0; i < rows * cols; ++i) {
          matrix[i] += 1;
        }

        print_matrix(matrix, rows, cols, "Add");

        // write to Chiplet2 RAM
        auto *data = new unsigned char[len];
        memcpy(data, response->get_data_ptr(), len);

        delete response;

        // cycle count simulated with Spike
        wait(2243 * config.get<sc_time>("cores.clk_cycle"));

        response = core.send_request(TLM_WRITE_COMMAND, 1, 2, 0x0, false, true,
                                     data, len);

        delete response;

        tracker->set_idle();
      }}},
    // Chiplet2 Core0
    {{2, 0},
     {[](chiplet::Core &core, UtilizationTracker *tracker) {},
      [](chiplet::Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {
        static const Config &config = ConfigRegistry::instance().get("Chiplet");

        tracker->set_active();

        auto addr = transaction->get_address();
        auto len = transaction->get_data_length();

        // read from Chiplet2 RAM
        auto *response = core.send_request(TLM_READ_COMMAND, 0, 2, addr, true,
                                           true, new unsigned char[len], len);

        // matrix header
        size_t header_size = sizeof(MatrixHeader);
        MatrixHeader *header =
            reinterpret_cast<MatrixHeader *>(response->get_data_ptr());
        uint32_t rows = header->rows;
        uint32_t cols = header->cols;

        // matrix
        int *matrix =
            reinterpret_cast<int *>(response->get_data_ptr() + header_size);

        // multiply by 2
        for (int i = 0; i < rows * cols; ++i) {
          matrix[i] *= 2;
        }

        print_matrix(matrix, rows, cols, "Multiply");

        // write to Chiplet3 RAM
        auto *data = new unsigned char[len];
        memcpy(data, response->get_data_ptr(), len);

        delete response;

        // cycle count simulated with Spike
        wait(2243 * config.get<sc_time>("cores.clk_cycle"));

        response = core.send_request(TLM_WRITE_COMMAND, 1, 3, 0x0, false, true,
                                     data, len);

        delete response;

        tracker->set_idle();
      }}},
    // Chiplet3 Core0
    {{3, 0},
     {[](chiplet::Core &core, UtilizationTracker *tracker) {},
      [](chiplet::Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {
        static const Config &config = ConfigRegistry::instance().get("Chiplet");

        tracker->set_active();

        auto addr = transaction->get_address();
        auto len = transaction->get_data_length();

        // read from Chiplet3 RAM
        auto *response = core.send_request(TLM_READ_COMMAND, 0, 3, addr, true,
                                           true, new unsigned char[len], len);

        // matrix header
        size_t header_size = sizeof(MatrixHeader);
        MatrixHeader *header =
            reinterpret_cast<MatrixHeader *>(response->get_data_ptr());
        uint32_t rows = header->rows;
        uint32_t cols = header->cols;

        // matrix
        int *matrix =
            reinterpret_cast<int *>(response->get_data_ptr() + header_size);

        // transpose
        int temp[rows * cols];
        for (int i = 0; i < rows; ++i) {
          for (int j = 0; j < cols; ++j) {
            temp[j * rows + i] = matrix[i * cols + j]; // transpose logic
          }
        }

        for (int i = 0; i < rows * cols; ++i) {
          matrix[i] = temp[i];
        }

        print_matrix(matrix, rows, cols, "Transpose");

        // write to Chiplet4 RAM
        auto *data = new unsigned char[len];
        memcpy(data, response->get_data_ptr(), len);

        delete response;

        // cycle count simulated with Spike
        wait(5516 * config.get<sc_time>("cores.clk_cycle"));

        response = core.send_request(TLM_WRITE_COMMAND, 1, 4, 0x0, false, true,
                                     data, len);

        delete response;

        tracker->set_idle();
      }}},
    // Chiplet4 Core0
    {{4, 0},
     {[](chiplet::Core &core, UtilizationTracker *tracker) {},
      [](chiplet::Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {
        static const Config &config = ConfigRegistry::instance().get("Chiplet");

        tracker->set_active();

        auto addr = transaction->get_address();
        auto len = transaction->get_data_length();

        // read from Chiplet4 RAM
        auto *response = core.send_request(TLM_READ_COMMAND, 0, 4, addr, true,
                                           true, new unsigned char[len], len);

        // matrix header
        size_t header_size = sizeof(MatrixHeader);
        MatrixHeader *header =
            reinterpret_cast<MatrixHeader *>(response->get_data_ptr());
        uint32_t rows = header->rows;
        uint32_t cols = header->cols;

        int *matrix =
            reinterpret_cast<int *>(response->get_data_ptr() + header_size);

        // subtract -5
        for (int i = 0; i < rows * cols; ++i) {
          matrix[i] -= 5;
        }

        print_matrix(matrix, rows, cols, "Subtract");

        // write to FPGA RAM
        auto *data = new unsigned char[len];
        memcpy(data, response->get_data_ptr(), len);

        delete response;

        // cycle count simulated with Spike
        wait(2243 * config.get<sc_time>("cores.clk_cycle"));

        response = core.send_request(TLM_WRITE_COMMAND, 1, 0, 0x0, false, true,
                                     data, len);

        delete response;

        tracker->set_idle();
      }}},
};