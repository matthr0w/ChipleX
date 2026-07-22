#pragma once

#include <cstdint>

struct IRQ {
	uint32_t request_id;
	uint8_t  target_module;
	uint32_t target_address;
	uint8_t  burst;
	unsigned data_length;
};