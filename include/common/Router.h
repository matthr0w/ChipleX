#pragma once

#include "setup/Types.h"

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

  int get_interconnect_id(int src_id, int dst_id) const;
  int get_link_id(int src_id, int interconnect_id, int dst_id) const;
  int get_dest_id(int src_id, int interconnect_id, int link_id) const;

private:
  Router() = default;

  SystemConfig sysconf_;

  struct RouteInfo {
    int interconnect_id = -1;
    int link_id = -1;
  };

  std::vector<std::vector<RouteInfo>> routing_table_;

  void build_table();
};
