#ifndef LOGGING_H
#define LOGGING_H

#include <iomanip>
#include <iostream>
#include <systemc.h>

#define SC_LOG_INFO(module, ...)                                               \
  do {                                                                         \
    std::cout << std::setw(9) << sc_time_stamp()                               \
              << " | \033[0mINFO\033[0m  | " << std::left << std::setw(20)     \
              << (module)->name() << " | " << __VA_ARGS__ << std::endl;        \
  } while (0)

#define SC_LOG_WARN(module, ...)                                               \
  do {                                                                         \
    std::cerr << std::setw(9) << sc_time_stamp()                               \
              << " | \033[33mWARN\033[0m  | " << std::left << std::setw(20)    \
              << (module)->name() << " | " << __VA_ARGS__ << std::endl;        \
  } while (0)

#define SC_LOG_ERROR(module, ...)                                              \
  do {                                                                         \
    std::cerr << std::setw(9) << sc_time_stamp()                               \
              << " | \033[31mERROR\033[0m | " << std::left << std::setw(20)    \
              << (module)->name() << " | " << __VA_ARGS__ << std::endl;        \
  } while (0)

#endif // SYSTEMC_LOGGER_H