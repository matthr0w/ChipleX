#pragma once

#include "common/Tracker.h"

#include "configs.h"
#include "globals.h"
#include "logging.h"

#include "modules/Core.h"

using CoreFunctions =
    std::pair<std::function<void(Core &, UtilizationTracker *)>, // main thread
              std::function<void(Core &, UtilizationTracker *,
                                 tlm_generic_payload *)> // interrupt handler
              >;
using CoreKey = std::pair<int, int>;

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

struct ImageHeader {
  uint32_t width;
  uint32_t height;
};

inline std::map<CoreKey, CoreFunctions> core_code = {
    // FPGA Core0
    {{0, 0},
     {[](Core &core, UtilizationTracker *tracker) {},
      [](Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {}}},
    // Chiplet1 Core0
    {{1, 0},
     {[](Core &core, UtilizationTracker *tracker) {
        int width, height, channels;
        unsigned char *input_img =
            stbi_load("include/usercode/tum_input.jpg", &width, &height, &channels, 3);

        unsigned header_size = sizeof(ImageHeader);
        unsigned img_size = width * height * channels;
        unsigned buffer_size = header_size + img_size;

        unsigned char *write_buffer = new unsigned char[buffer_size];
        unsigned char *read_buffer = new unsigned char[buffer_size];

        ImageHeader *header = reinterpret_cast<ImageHeader *>(write_buffer);
        header->width = width;
        header->height = height;

        std::memcpy(write_buffer + header_size, input_img, img_size);

        stbi_image_free(input_img);

        Core::RequestHandle *h = nullptr;
        unsigned offset = 0;
        while (offset < buffer_size) {
          unsigned chunk_size =
              std::min(core.MAX_INCR_BURST_SIZE, buffer_size - offset);

          h = core.write(
              1, 1, 0x0 + offset,
              reinterpret_cast<unsigned char *>(write_buffer + offset),
              chunk_size, true);

          offset += chunk_size;
        }

        h->wait();

        delete[] write_buffer;

        offset = 0;
        while (offset < buffer_size) {
          unsigned chunk_size =
              std::min(core.MAX_INCR_BURST_SIZE, buffer_size - offset);

          h = core.read(2, 1, 0x0 + offset,
                        reinterpret_cast<unsigned char *>(read_buffer + offset),
                        chunk_size, true);

          offset += chunk_size;
        }

        h->wait();

        header = reinterpret_cast<ImageHeader *>(read_buffer);
        width = header->width;
        height = header->height;

        unsigned char *img_data = read_buffer + sizeof(ImageHeader);

        stbi_write_jpg("tum_output.jpg", width, height, 3, img_data,
                       100);

        delete[] read_buffer;

        sc_stop();
      },
      [](Core &core, UtilizationTracker *tracker,
         tlm_generic_payload *transaction) {}}}};