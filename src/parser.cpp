#include "parser.h"

#include "globals.h"

int Parser::parse(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help") {
      print_help(argv[0]);
      return 0;
    } else if (arg.rfind("--time=", 0) == 0) {
      try {
        double value = std::stod(arg.substr(7));
        sim_duration = sc_time(value, SC_NS);
      } catch (...) {
        std::cerr << "Invalid value for --time\n";
        return 1;
      }
    } else if (arg.rfind("--ber=", 0) == 0) {
      try {
        bit_error_rate = std::stod(arg.substr(6));
      } catch (...) {
        std::cerr << "Invalid value for --ber\n";
        return 1;
      }
    } else if (arg.rfind("--logging=", 0) == 0) {
      std::string level = arg.substr(10);
      std::transform(level.begin(), level.end(), level.begin(), ::tolower);

      if (level == "info") {
        log_level = LogLevel::INFO;
      } else if (level == "warn") {
        log_level = LogLevel::WARN;
      } else if (level == "error") {
        log_level = LogLevel::ERROR;
      } else if (level == "debug") {
        log_level = LogLevel::DEBUG;
      } else if (level == "delay") {
        log_level = LogLevel::DELAY;
      } else if (level == "silent") {
        log_level = LogLevel::SILENT;
      } else {
        std::cerr << "Unknown logging level: " << level << "\n";
        print_help(argv[0]);
        return 1;
      }
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      print_help(argv[0]);
      return 1;
    }
  }

  return -1;
}

void Parser::print_help(const char *progname) {
  std::cout
      << "Usage: " << progname << " [options]\n"
      << "Options:\n"
      << "  --time=<ns>               Set simulation time in nanoseconds "
         "(default: unlimited)\n"
      << "  --ber=<prob>              Set bit error rate (default: 1e-12)\n"
      << "  --logging=level           Set logging level: INFO, WARN, "
         "ERROR, DELAY, DEBUG, SILENT (default: ERROR)\n"
      << "  --help                    Show this help message\n";
}