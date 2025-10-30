#include "program.h"

#include "modules/Core.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

struct ImageHeader {
  uint32_t width;
  uint32_t height;
  uint32_t channels;
};

static unsigned TOTAL_PASSES = 10;

CoreCodeMap *get_program_code() {
  static CoreCodeMap code = {
      {{"fpga", 0},
       {// Main thread: Loads image, sends it frame by frame
        [](Core &core) {
          static unsigned request_count = 0;
          static sc_time request_delay(8, SC_MS); // ~125 FPS

          int width = 0, height = 0, channels = 0;
          unsigned char *input_image =
              stbi_load("setups/duckcam/data/duckiebot_input.jpg", &width,
                        &height, &channels, 3);

          const size_t header_size = sizeof(ImageHeader);
          const size_t image_size = width * height * channels;
          const size_t buffer_size = header_size + image_size;

          while (true) {
            auto *buffer = new unsigned char[buffer_size];

            auto *header = reinterpret_cast<ImageHeader *>(buffer);
            header->width = width;
            header->height = height;
            header->channels = channels;

            std::memcpy(buffer + header_size, input_image, image_size);

            sc_time start_stamp = sc_time_stamp();

            size_t max_size = core.MAX_INCR_BURST_SIZE;
            size_t offset = 0;
            int req_id = 0;

            while (offset < buffer_size) {
              size_t chunk_size = std::min(buffer_size - offset, max_size);

              auto reqw =
                  Core::WriteRequest(req_id, buffer + offset, chunk_size)
                      .set_dest(1)
                      .skip_cache();

              core.write(reqw);
              offset += chunk_size;
              ++req_id;
            }

            sc_time end_stamp = sc_time_stamp();
            ++request_count;

            if (request_count == TOTAL_PASSES)
              break;

            wait(request_delay - (end_stamp - start_stamp));
            delete[] buffer;
          }

          stbi_image_free(input_image);
        },
        // Interrupt handler: Saves processed images
        [](Core &core, tlm_generic_payload *irq) {
          static unsigned pass = 0;

          uint32_t addr = irq->get_address();
          size_t length = irq->get_data_length();

          auto *read_buf = new unsigned char[length];

          size_t max_size = core.MAX_INCR_BURST_SIZE;
          size_t offset = 0;
          int req_id = 0;
          std::shared_ptr<Core::RequestHandle> handle = nullptr;

          while (offset < length) {
            size_t chunk_size = std::min(length - offset, max_size);

            auto reqr = Core::ReadRequest(req_id, addr + offset,
                                          read_buf + offset, chunk_size)
                            .set_dest(0)
                            .skip_cache();

            handle = core.read(reqr);
            offset += chunk_size;
            ++req_id;
          }

          handle->wait();

          auto *header = reinterpret_cast<ImageHeader *>(read_buf);
          unsigned char *img_data = read_buf + sizeof(ImageHeader);

          std::string filename = "setups/duckcam/data/duckiebot_output" +
                                 std::to_string(pass) + ".jpg";

          stbi_write_jpg(filename.c_str(), header->width, header->height,
                         header->channels, img_data, 100);

          delete[] read_buf;
          ++pass;

          if (pass == TOTAL_PASSES)
            sc_stop();
        }}},

      {{"chiplet0", 0},
       {[](Core &) {},
        [](Core &core, tlm_generic_payload *irq) {
          static unsigned interrupt_count = 0;
          static uint32_t base_addr = 0;
          static size_t total_len = 0;

          if (interrupt_count == 0)
            base_addr = irq->get_address();

          total_len += irq->get_data_length();
          ++interrupt_count;

          if (interrupt_count != 3)
            return;

          auto *read_buf = new unsigned char[total_len];
          size_t max_size = core.MAX_INCR_BURST_SIZE;
          size_t offset = 0;
          int req_id = 0;
          std::shared_ptr<Core::RequestHandle> handle = nullptr;

          while (offset < total_len) {
            size_t chunk_size = std::min(total_len - offset, max_size);

            auto reqr = Core::ReadRequest(req_id, base_addr + offset,
                                          read_buf + offset, chunk_size)
                            .set_dest(1)
                            .skip_cache();

            handle = core.read(reqr);
            offset += chunk_size;
            ++req_id;
          }

          handle->wait();

          const size_t header_size = sizeof(ImageHeader);
          auto *header = reinterpret_cast<ImageHeader *>(read_buf);

          const int crop_margin = 12;
          uint32_t new_width = header->width;
          uint32_t new_height = header->height - crop_margin;
          uint32_t channels = header->channels;

          size_t new_img_size = new_width * new_height * 3;
          size_t new_buf_size = header_size + new_img_size;

          auto *write_buf = new unsigned char[new_buf_size];
          auto *new_header = reinterpret_cast<ImageHeader *>(write_buf);
          new_header->width = new_width;
          new_header->height = new_height;
          new_header->channels = channels;

          auto *src = read_buf + header_size;
          auto *dst = write_buf + header_size;

          for (uint32_t y = 0; y < new_height; ++y)
            std::memcpy(dst + y * new_width * 3,
                        src + (y + crop_margin) * new_width * 3, new_width * 3);

          core.wait_cycles("crop");

          offset = 0;
          while (offset < new_buf_size) {
            size_t chunk_size = std::min(new_buf_size - offset, max_size);

            auto reqw =
                Core::WriteRequest(req_id, write_buf + offset, chunk_size)
                    .set_dest(2)
                    .skip_cache();

            handle = core.write(reqw);
            offset += chunk_size;
            ++req_id;
          }

          handle->wait();

          delete[] read_buf;
          delete[] write_buf;

          interrupt_count = 0;
          base_addr = 0;
          total_len = 0;
        }}},

      {{"chiplet1", 0},
       {[](Core &) {},
        [](Core &core, tlm_generic_payload *irq) {
          static unsigned interrupt_count = 0;
          static uint32_t base_addr = 0;
          static size_t total_len = 0;

          if (interrupt_count == 0)
            base_addr = irq->get_address();

          total_len += irq->get_data_length();
          ++interrupt_count;

          if (interrupt_count != 2)
            return;

          auto *read_buf = new unsigned char[total_len];
          size_t max_size = core.MAX_INCR_BURST_SIZE;
          size_t offset = 0;
          int req_id = 0;
          std::shared_ptr<Core::RequestHandle> handle = nullptr;

          while (offset < total_len) {
            size_t chunk_size = std::min(total_len - offset, max_size);

            auto reqr = Core::ReadRequest(req_id, base_addr + offset,
                                          read_buf + offset, chunk_size)
                            .set_dest(2)
                            .skip_cache();

            handle = core.read(reqr);
            offset += chunk_size;
            ++req_id;
          }

          handle->wait();

          const size_t header_size = sizeof(ImageHeader);
          auto *header = reinterpret_cast<ImageHeader *>(read_buf);
          uint32_t width = header->width;
          uint32_t height = header->height;

          size_t new_img_size = width * height;
          size_t new_buf_size = header_size + new_img_size;

          auto *write_buf = new unsigned char[new_buf_size];
          std::memcpy(write_buf, read_buf, header_size);

          auto *new_header = reinterpret_cast<ImageHeader *>(write_buf);
          new_header->channels = 1;

          auto *src = read_buf + header_size;
          auto *dst = write_buf + header_size;

          for (size_t i = 0; i < new_img_size; ++i) {
            unsigned char r = src[i * 3 + 0];
            unsigned char g = src[i * 3 + 1];
            unsigned char b = src[i * 3 + 2];

            dst[i] =
                static_cast<unsigned char>(0.299 * r + 0.587 * g + 0.114 * b);
          }

          core.wait_cycles("grayscale");

          offset = 0;
          while (offset < new_buf_size) {
            size_t chunk_size = std::min(new_buf_size - offset, max_size);

            auto reqw =
                Core::WriteRequest(req_id, write_buf + offset, chunk_size)
                    .set_dest(0)
                    .skip_cache();

            handle = core.write(reqw);
            offset += chunk_size;
            ++req_id;
          }

          handle->wait();

          delete[] read_buf;
          delete[] write_buf;

          interrupt_count = 0;
          base_addr = 0;
          total_len = 0;
        }}}};
  return &code;
}