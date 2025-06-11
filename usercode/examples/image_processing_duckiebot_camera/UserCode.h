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

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

struct ImageHeader {
  uint32_t width;
  uint32_t height;
  uint32_t channels;
};

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

inline GeneratorFunctions generator_code = {
    [](fpga::Generator &gen, UtilizationTracker *tracker) {
      // FPGA GENERATOR CODE BELOW
      static unsigned int request = 0;
      static sc_time request_delay(8, SC_MS); // approximately 120 fps

      int width, height, channels;
      unsigned char *input_img = stbi_load("configs/duckiebot_input.jpg",
                                           &width, &height, &channels, 3);

      size_t header_size = sizeof(ImageHeader);
      size_t img_size = width * height * channels;
      size_t buffer_size = header_size + img_size;

      while (true) {
        tracker->set_active();

        unsigned char *buffer = new unsigned char[buffer_size];

        ImageHeader *header = reinterpret_cast<ImageHeader *>(buffer);
        header->width = width;
        header->height = height;
        header->channels = channels;

        std::memcpy(buffer + header_size, input_img, img_size);

        sc_time request_start_stamp = sc_time_stamp();

        // write to Chiplet1 RAM
        auto response = gen.send_request(TLM_WRITE_COMMAND, request, 1, 0x0,
                                         false, buffer, buffer_size);

        delete response;

        sc_time request_end_stamp = sc_time_stamp();

        ++request;

        tracker->set_idle();

        if (request == 10) {
          break;
        }

        wait(request_delay - (request_end_stamp - request_start_stamp));
      }

      stbi_image_free(input_img);
      // ------------------------------
    },
    [](fpga::Generator &gen, UtilizationTracker *tracker,
       tlm_generic_payload *transaction) {
      // FPGA INTERRUPT HANDLER CODE BELOW
      static unsigned int request = 0;

      tracker->set_active();

      auto addr = transaction->get_address();
      auto len = transaction->get_data_length();

      // read from FPGA RAM
      unsigned char *buffer = new unsigned char[len];
      auto *response = gen.send_request(TLM_READ_COMMAND, request, 0, addr,
                                        true, buffer, len);

      ImageHeader *header = reinterpret_cast<ImageHeader *>(buffer);
      uint32_t width = header->width;
      uint32_t height = header->height;
      uint32_t channels = header->channels;

      unsigned char *img_data = buffer + sizeof(ImageHeader);

      std::string filename =
          "configs/duckiebot_output" + std::to_string(request) + ".jpg";

      stbi_write_jpg(filename.c_str(), width, height, channels, img_data, 100);

      delete response;

      ++request;

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
        static unsigned int request = 0;

        tracker->set_active();

        auto addr = transaction->get_address();
        auto len = transaction->get_data_length();

        // read from Chiplet1 RAM
        unsigned char *read_buffer = new unsigned char[len];
        auto response = core.send_request(TLM_READ_COMMAND, request, 1, addr,
                                          false, read_buffer, len);

        // image header
        size_t header_size = sizeof(ImageHeader);
        ImageHeader *header = reinterpret_cast<ImageHeader *>(read_buffer);
        uint32_t width = header->width;
        uint32_t height = header->height;
        uint32_t channels = header->channels;

        // update image header
        const int crop_margin = 12;
        uint32_t new_width = width;
        uint32_t new_height = height - crop_margin;

        size_t new_img_size = new_width * new_height * 3;
        size_t new_buffer_size = header_size + new_img_size;

        unsigned char *write_buffer = new unsigned char[new_buffer_size];

        ImageHeader *new_header = reinterpret_cast<ImageHeader *>(write_buffer);
        new_header->width = new_width;
        new_header->height = new_height;
        new_header->channels = channels;

        // crop image
        unsigned char *src_pixels = read_buffer + header_size;
        unsigned char *dst_pixels = write_buffer + header_size;

        for (uint32_t y = 0; y < new_height; ++y) {
          unsigned char *src_row = src_pixels + ((y + crop_margin) * width) * 3;
          unsigned char *dst_row = dst_pixels + (y * new_width) * 3;
          std::memcpy(dst_row, src_row, new_width * 3);
        }

        delete response;

        // cycle count simulated with Spike
        wait(1347 * config.get<sc_time>("cores.clk_cycle"));

        // write to Chiplet2 RAM
        response = core.send_request(TLM_WRITE_COMMAND, request, 2, 0x0, false,
                                     write_buffer, new_buffer_size);

        delete response;

        ++request;

        tracker->set_idle();
      }}},
    // Chiplet2 Core0
    {{2, 0},
     {[](chiplet::Core &core, UtilizationTracker *tracker) {},
      [](chiplet::Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {
        static const Config &config = ConfigRegistry::instance().get("Chiplet");
        static unsigned int request = 0;

        tracker->set_active();

        auto addr = transaction->get_address();
        auto len = transaction->get_data_length();

        // read from Chiplet2 RAM
        unsigned char *read_buffer = new unsigned char[len];
        auto response = core.send_request(TLM_READ_COMMAND, request, 2, addr, false,
                                          read_buffer, len);

        // image header
        const size_t header_size = sizeof(ImageHeader);
        ImageHeader *header = reinterpret_cast<ImageHeader *>(read_buffer);
        uint32_t width = header->width;
        uint32_t height = header->height;

        // update header
        size_t new_img_size = width * height * 1;
        size_t new_buffer_size = header_size + new_img_size;

        unsigned char *write_buffer = new unsigned char[new_buffer_size];
        std::memcpy(write_buffer, read_buffer, header_size);

        ImageHeader *new_header = reinterpret_cast<ImageHeader *>(write_buffer);
        new_header->channels = 1;

        // convert to grayscale
        unsigned char *src_pixels = read_buffer + header_size;
        unsigned char *dst_pixels = write_buffer + header_size;

        for (size_t i = 0; i < new_img_size; ++i) {
          unsigned char r = src_pixels[i * 3 + 0];
          unsigned char g = src_pixels[i * 3 + 1];
          unsigned char b = src_pixels[i * 3 + 2];

          // standard grayscale conversion
          unsigned char gray =
              static_cast<unsigned char>(0.299 * r + 0.587 * g + 0.114 * b);

          dst_pixels[i] = gray;
        }

        delete response;

        // cycle count simulated with Spike
        wait(54006 * config.get<sc_time>("cores.clk_cycle"));

        // write to FPGA RAM
        response = core.send_request(TLM_WRITE_COMMAND, request, 0, 0x0, false,
                                     write_buffer, new_buffer_size);

        delete response;

        ++request;

        tracker->set_idle();
      }}},
};