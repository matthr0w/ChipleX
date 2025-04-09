#ifndef LOGGING_H
#define LOGGING_H

#include <iomanip>
#include <iostream>
#include <systemc.h>

const bool DEBUG = true;

#define SC_LOG_DEBUG(module, ...)                                              \
  do {                                                                         \
    if (DEBUG) {                                                               \
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

#endif // SYSTEMC_LOGGER_H