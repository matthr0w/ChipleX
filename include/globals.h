#pragma once

#include <vector>

enum class LogLevel {
  DEBUG = 0,
  INFO = 1,
  WARN = 2,
  ERROR = 3,
  SILENT = 4
};

extern LogLevel log_level;

extern unsigned int num_chiplets;
extern std::vector<unsigned int> connections;