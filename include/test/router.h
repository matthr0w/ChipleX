#pragma once

#include "system.h"

class Router {
public:
  Router(const Router &) = delete;
  Router &operator=(const Router &) = delete;

  static Router &instance() {
    static Router instance_;
    return instance_;
  }

  void init(const SystemConfig &sysconf) {
    sysconf_ = sysconf;
    build_table();
  }

  void print_table() const;

  int get_link_id(int src_id, int dst_id) const;
  int get_dest_id(int src_id, int link_id) const;

private:
  Router() = default;
  void build_table();

  SystemConfig sysconf_;
  std::vector<std::vector<int>> routing_table_;
};
