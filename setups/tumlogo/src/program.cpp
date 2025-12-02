#include "program.h"

#include "modules/Core.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

struct ImageHeader {
  uint32_t width;
  uint32_t height;
};

ModuleCodeMap *get_program_code() {
  static ModuleCodeMap code = {
      {{"fpga", "core0"},
       {CPUCode{.main =
                    [](Core &core) {
                      int width = 0, height = 0, channels = 0;
                      unsigned char *input_img =
                          stbi_load("setups/tumlogo/data/tum_input.jpg", &width,
                                    &height, &channels, 3);

                      size_t header_size = sizeof(ImageHeader);
                      size_t img_size = width * height * channels;
                      size_t buffer_size = header_size + img_size;

                      unsigned char *write_buf = new unsigned char[buffer_size];

                      ImageHeader *header =
                          reinterpret_cast<ImageHeader *>(write_buf);
                      header->width = width;
                      header->height = height;

                      std::memcpy(write_buf + header_size, input_img, img_size);

                      size_t max_size = core.MAX_INCR_BURST_SIZE;
                      size_t offset = 0;
                      int req_id = 0;
                      std::shared_ptr<RequestHandle> handle = nullptr;

                      while (offset < buffer_size) {
                        size_t chunk_size =
                            std::min(buffer_size - offset, max_size);

                        auto reqw =
                            AxiRequest(req_id, write_buf + offset, chunk_size)
                                .to_via("chiplet0", "memory", "interconnect")
                                .skip_cache();

                        handle = core.write(reqw);
                        offset += chunk_size;
                        ++req_id;
                      }

                      handle->wait();

                      delete[] write_buf;

                      stbi_image_free(input_img);
                    },
                .irq =
                    [](Core &core, const IRQ &irq) {
                      static unsigned interrupt_count = 0;
                      static uint32_t base_addr = 0;
                      static size_t total_len = 0;

                      if (interrupt_count == 0)
                        base_addr = irq.target_address;

                      total_len += irq.data_length;
                      ++interrupt_count;

                      if (interrupt_count != 118)
                        return;

                      auto *read_buf = new unsigned char[total_len];

                      size_t max_size = core.MAX_INCR_BURST_SIZE;
                      size_t offset = 0;
                      int req_id = 0;
                      std::shared_ptr<RequestHandle> handle = nullptr;

                      while (offset < total_len) {
                        size_t chunk_size =
                            std::min(total_len - offset, max_size);

                        auto reqr =
                            AxiRequest(req_id, read_buf + offset, chunk_size)
                                .set_addr(base_addr + offset)
                                .skip_cache();

                        handle = core.read(reqr);
                        offset += chunk_size;
                        ++req_id;
                      }

                      handle->wait();

                      ImageHeader *header =
                          reinterpret_cast<ImageHeader *>(read_buf);
                      auto *img_data = read_buf + sizeof(ImageHeader);

                      stbi_write_jpg("setups/tumlogo/data/tum_output.jpg",
                                     header->width, header->height, 3, img_data,
                                     100);

                      delete[] read_buf;

                      sc_stop();
                    }}}},

      {{"chiplet0", "core0"},
       {CPUCode{
           .main = [](Core &core) {},
           .irq =
               [](Core &core, const IRQ &irq) {
                 static unsigned interrupt_count = 0;
                 static uint32_t base_addr = 0;
                 static size_t total_len = 0;

                 if (interrupt_count == 0)
                   base_addr = irq.target_address;

                 total_len += irq.data_length;
                 ++interrupt_count;

                 if (interrupt_count != 193)
                   return;

                 auto *read_buf = new unsigned char[total_len];
                 size_t max_size = core.MAX_INCR_BURST_SIZE;
                 size_t offset = 0;
                 int req_id = 0;
                 std::shared_ptr<RequestHandle> handle = nullptr;

                 while (offset < total_len) {
                   size_t chunk_size = std::min(total_len - offset, max_size);

                   auto reqr = AxiRequest(req_id, read_buf + offset, chunk_size)
                                   .set_addr(base_addr + offset)
                                   .skip_cache();

                   handle = core.read(reqr);
                   offset += chunk_size;
                   ++req_id;
                 }

                 handle->wait();

                 const size_t header_size = sizeof(ImageHeader);
                 auto *header = reinterpret_cast<ImageHeader *>(read_buf);

                 const int crop_margin = 28;
                 uint32_t new_width = header->width - 2 * crop_margin;
                 uint32_t new_height = header->height - 2 * crop_margin;

                 size_t new_img_size = new_width * new_height * 3;
                 size_t new_buf_size = header_size + new_img_size;

                 auto *write_buf = new unsigned char[new_buf_size];
                 auto *new_header = reinterpret_cast<ImageHeader *>(write_buf);
                 new_header->width = new_width;
                 new_header->height = new_height;

                 auto *src = read_buf + header_size;
                 auto *dst = write_buf + header_size;

                 for (uint32_t y = 0; y < new_height; ++y) {
                   unsigned char *src_row =
                       src +
                       ((y + crop_margin) * header->width + crop_margin) * 3;
                   unsigned char *dst_row = dst + (y * new_width) * 3;
                   std::memcpy(dst_row, src_row, new_width * 3);
                 }

                 core.wait_cycles("crop");

                 offset = 0;
                 while (offset < new_buf_size) {
                   size_t chunk_size =
                       std::min(new_buf_size - offset, max_size);

                   auto reqw =
                       AxiRequest(req_id, write_buf + offset, chunk_size)
                           .to_via("chiplet1", "memory", "interconnect")
                           .skip_cache();

                   handle = core.write(reqw);
                   offset += chunk_size;
                   ++req_id;
                 }

                 handle->wait();

                 delete[] read_buf;
                 delete[] write_buf;
               }}}},

      {{"chiplet1", "core0"},
       {CPUCode{.main = [](Core &core) {},
                .irq =
                    [](Core &core, const IRQ &irq) {
                      static unsigned interrupt_count = 0;
                      static uint32_t base_addr = 0;
                      static size_t total_len = 0;

                      if (interrupt_count == 0)
                        base_addr = irq.target_address;

                      total_len += irq.data_length;
                      ++interrupt_count;

                      if (interrupt_count != 118)
                        return;

                      auto *read_buf = new unsigned char[total_len];
                      size_t max_size = core.MAX_INCR_BURST_SIZE;
                      size_t offset = 0;
                      int req_id = 0;
                      std::shared_ptr<RequestHandle> handle = nullptr;

                      while (offset < total_len) {
                        size_t chunk_size =
                            std::min(total_len - offset, max_size);

                        auto reqr =
                            AxiRequest(req_id, read_buf + offset, chunk_size)
                                .set_addr(base_addr + offset)
                                .skip_cache();

                        handle = core.read(reqr);
                        offset += chunk_size;
                        ++req_id;
                      }

                      handle->wait();

                      const size_t header_size = sizeof(ImageHeader);
                      auto *header = reinterpret_cast<ImageHeader *>(read_buf);

                      auto *write_buf = new unsigned char[total_len];
                      std::memcpy(write_buf, read_buf, header_size);

                      for (size_t i = header_size; i < total_len; ++i)
                        write_buf[i] = 255 - read_buf[i];

                      core.wait_cycles("invert");

                      offset = 0;
                      while (offset < total_len) {
                        size_t chunk_size =
                            std::min(total_len - offset, max_size);

                        auto reqw =
                            AxiRequest(req_id, write_buf + offset, chunk_size)
                                .to_via("fpga", "memory", "interconnect")
                                .skip_cache();

                        handle = core.write(reqw);
                        offset += chunk_size;
                        ++req_id;
                      }

                      handle->wait();

                      delete[] read_buf;
                      delete[] write_buf;
                    }}}}};
  return &code;
}