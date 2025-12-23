#pragma once

#include <cstdint>

namespace SmartExtension {
enum ID : uint8_t { NOOP = 0, CRYPTO = 1 };

const unsigned MAX = 8;
} // namespace SmartExtension