#include "program.h"

#include "modules/Core.h"

#include "lz4.h"

struct DataHeader {
	uint32_t decompressed_data_size;
	uint32_t compressed_data_size;
};

ModuleCodeMap *get_program_code() {
	static ModuleCodeMap code = {
	    {{"fpga", "core0"},
	     {CPUCode{.main =
	                  [](Core &core) {
		                  std::string str = "Hello, World! This is a test string for the LZ4 "
		                                    "compression algorithm. Hello, World! This is a test "
		                                    "string for the LZ4 compression algorithm.";

		                  LOG_INFO("Input string: " << str);

		                  auto *write_buf = new unsigned char[str.length() + 1];
		                  std::strcpy(reinterpret_cast<char *>(write_buf), str.c_str());

		                  auto reqw =
		                      AxiRequest(0, write_buf, str.length() + 1).to_via("chiplet0", "memory", "interconnect");

		                  auto handle = core.write(reqw);

		                  handle->wait();

		                  delete[] write_buf;
	                  },
	              .irq =
	                  [](Core &core, const IRQ &irq) {
		                  auto addr = irq.target_address;
		                  auto len  = irq.data_length;

		                  // Read from FPGA RAM
		                  auto *read_buf = new unsigned char[len];
		                  auto  reqr     = AxiRequest(0, read_buf, len).set_addr(addr);
		                  auto  handle   = core.read(reqr);
		                  handle->wait();

		                  // Print result
		                  LOG_INFO("Output string: " << read_buf);

		                  delete[] read_buf;

		                  sc_stop();
	                  }}}},

	    {{"chiplet0", "core0"},
	     {CPUCode{.main = [](Core &core) {},
	              .irq =
	                  [](Core &core, const IRQ &irq) {
		                  auto addr = irq.target_address;
		                  auto len  = irq.data_length;

		                  // Read from Chiplet0 RAM
		                  auto *read_buf = new unsigned char[len];
		                  auto  reqr     = AxiRequest(0, read_buf, len).set_addr(addr);
		                  auto  handle   = core.read(reqr);
		                  handle->wait();

		                  // Calculate compression sizes
		                  const int max_compressed_size = LZ4_compressBound(len);
		                  auto     *compressed_data     = new unsigned char[max_compressed_size];

		                  // Compress
		                  const int compressed_data_size =
		                      LZ4_compress_default(reinterpret_cast<const char *>(read_buf),
		                                           reinterpret_cast<char *>(compressed_data), len, max_compressed_size);

		                  if (compressed_data_size < 0) {
			                  LOG_ERROR("Compression failed!");
		                  } else {
			                  LOG_INFO("Compression succeeded! Original size: " << len << " | Compressed size: "
			                                                                    << compressed_data_size);
		                  }

		                  // Build header + payload
		                  DataHeader header;
		                  header.decompressed_data_size = len;
		                  header.compressed_data_size   = compressed_data_size;

		                  const int total_size = sizeof(DataHeader) + compressed_data_size;
		                  auto     *packet     = new unsigned char[total_size];

		                  std::memcpy(packet, &header, sizeof(DataHeader));
		                  std::memcpy(packet + sizeof(DataHeader), compressed_data, compressed_data_size);

		                  core.wait_cycles("compress");

		                  // Write to Chiplet1 RAM
		                  auto reqw = AxiRequest(0, packet, total_size).to_via("chiplet1", "memory", "interconnect");
		                  handle    = core.write(reqw);
		                  handle->wait();

		                  delete[] read_buf;
		                  delete[] compressed_data;
		                  delete[] packet;
	                  }}}},

	    {{"chiplet1", "core0"},
	     {CPUCode{
	         .main = [](Core &core) {},
	         .irq =
	             [](Core &core, const IRQ &irq) {
		             auto addr = irq.target_address;
		             auto len  = irq.data_length;

		             // Read from Chiplet1 RAM
		             auto *read_buf = new unsigned char[len];
		             auto  reqr     = AxiRequest(0, read_buf, len).set_addr(addr);
		             auto  handle   = core.read(reqr);
		             handle->wait();

		             // Extract header
		             DataHeader header;
		             std::memcpy(&header, read_buf, sizeof(DataHeader));

		             const auto decompressed_size = header.decompressed_data_size;
		             const auto compressed_size   = header.compressed_data_size;

		             // Allocate decompression buffer
		             auto *decompressed_data = new unsigned char[decompressed_size];

		             // Decompress using header offsets
		             const int result = LZ4_decompress_safe(
		                 reinterpret_cast<const char *>(read_buf + sizeof(DataHeader)),
		                 reinterpret_cast<char *>(decompressed_data), compressed_size, decompressed_size);

		             if (result < 0) {
			             LOG_ERROR("Decompression failed!");
		             } else {
			             LOG_INFO("Decompression succeeded! Compressed size: " << compressed_size
			                                                                   << " | Decompressed size: " << result);
		             }

		             core.wait_cycles("decompress");

		             // Write back to FPGA RAM
		             auto reqw =
		                 AxiRequest(0, decompressed_data, decompressed_size).to_via("fpga", "memory", "interconnect");

		             handle = core.write(reqw);
		             handle->wait();

		             delete[] read_buf;
		             delete[] decompressed_data;
	             }}}     }
    };
	return &code;
}