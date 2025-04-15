#pragma once

#include <systemc.h>

#include <iomanip>
#include <iostream>

const bool debug_msgs = true;

const std::array<std::string, 6> modules = {
    "Free", "Core1", "Core2", "RAM", "Interconnect1", "Interconnect2"};

#define SC_LOG_DEBUG(module, ...)                                              \
  do {                                                                         \
    if (debug_msgs) {                                                          \
      std::cout << std::setw(9) << sc_time_stamp()                             \
                << " | \033[34m[DEBUG]\033[0m  | " << std::left                \
                << std::setw(20) << (module)->name() << " | " << __VA_ARGS__   \
                << std::endl;                                                  \
    }                                                                          \
  } while (0)

#define SC_LOG_INFO(module, ...)                                               \
  do {                                                                         \
    std::cout << std::setw(9) << sc_time_stamp()                               \
              << " | \033[0m[INFO]\033[0m   | " << std::left << std::setw(20)  \
              << (module)->name() << " | " << __VA_ARGS__ << std::endl;        \
  } while (0)

#define SC_LOG_WARN(module, ...)                                               \
  do {                                                                         \
    std::cerr << std::setw(9) << sc_time_stamp()                               \
              << " | \033[33m[WARN]\033[0m   | " << std::left << std::setw(20) \
              << (module)->name() << " | " << __VA_ARGS__ << std::endl;        \
  } while (0)

#define SC_LOG_ERROR(module, ...)                                              \
  do {                                                                         \
    std::cerr << std::setw(9) << sc_time_stamp()                               \
              << " | \033[31m[ERROR]\033[0m  | " << std::left << std::setw(20) \
              << (module)->name() << " | " << __VA_ARGS__ << std::endl;        \
  } while (0)
