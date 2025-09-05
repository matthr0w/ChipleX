#include "modules/AXIBus.h"

#include <iomanip>

#include "globals.h"

AXIBus::AXIBus(sc_module_name name, unsigned int chip_id,
               unsigned int axi_width, unsigned int num_managers,
               unsigned int num_subordinates)
    : sc_module(name), chip_id(chip_id), axi_width(axi_width),
      beat_data(new uint8_t[axi_width >> 3]) {
  sub_state.resize(num_subordinates);

  mgr_tsockets.reserve(num_managers);
  for (unsigned i = 0; i < num_managers; ++i) {
    std::ostringstream name;
    name << "tsocket" << i;
    mgr_tsockets.emplace_back(
        std::make_unique<ARM::AXI4::SimpleTargetSocketTagged<AXIBus>>(
            name.str().c_str(), *this, i, &AXIBus::nb_transport_fw,
            ARM::TLM::PROTOCOL_AXI4, axi_width));
  }

  sub_isockets.reserve(num_subordinates);
  for (unsigned i = 0; i < num_subordinates; ++i) {
    std::ostringstream name;
    name << "isocket" << i;
    sub_isockets.emplace_back(
        std::make_unique<ARM::AXI4::SimpleInitiatorSocketTagged<AXIBus>>(
            name.str().c_str(), *this, i, &AXIBus::nb_transport_bw,
            ARM::TLM::PROTOCOL_AXI4, axi_width));
  }
}

AXIBus::~AXIBus() { delete[] beat_data; }

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum AXIBus::nb_transport_fw(int mgr_id, ARM::AXI::Payload &payload,
                                      ARM::AXI::Phase &phase) {
  std::scoped_lock lock(fw_mutex);

  int sub_id = 0;
  if (auto it = payloads2sub.find(&payload); it != payloads2sub.end())
    sub_id = it->second;
  else {
    sub_id = route_payload(payload);
    payloads2sub[&payload] = sub_id;
    payloads2mgr[&payload] = mgr_id;
  }

  SubState &S = sub_state[sub_id];

  // ---- READ CHANNEL ARBITRATION ----
  if (is_ar_valid(phase)) {
    if (!S.R.locked) {
      // lock read channel for this payload
      S.R.locked = true;
      S.R.cur = &payload;
      S.R.mgr = mgr_id;
    } else if (S.R.cur != &payload) {
      // serialize: another read in progress on this subordinate
      // keep phase unchanged -> manager will retry next cycle
      return TLM_ACCEPTED;
    }
  }

  // ---- WRITE CHANNEL ARBITRATION ----
  if (is_aw_valid(phase)) {
    if (!S.W.locked) {
      // lock write channel for this payload
      S.W.locked = true;
      S.W.cur = &payload;
      S.W.mgr = mgr_id;
    } else if (S.W.cur != &payload) {
      // serialize: another write in progress on this subordinate
      // keep phase unchanged -> manager will retry next cycle
      return TLM_ACCEPTED;
    }
  }

  // write data beats can only flow for the locked write tx
  if (is_w_valid(phase) || is_w_valid_last(phase)) {
    if (!S.W.locked || S.W.cur != &payload) {
      return TLM_ACCEPTED; // stall W beats until AW locked this channel
    }
  }

  // forward to subordinate
  ARM::AXI::Phase prev_phase = phase;
  tlm_sync_enum reply = sub_isockets[sub_id]->nb_transport_fw(payload, phase);

  if (log_level <= LogLevel::DEBUG)
    print_payload(payload, prev_phase, reply, phase);

  return reply;
}

tlm_sync_enum AXIBus::nb_transport_bw(int sub_id, ARM::AXI::Payload &payload,
                                      ARM::AXI::Phase &phase) {
  std::scoped_lock lock(bw_mutex);

  // find manager for this payload
  int mgr_id = 0;
  if (auto it = payloads2mgr.find(&payload); it != payloads2mgr.end())
    mgr_id = it->second;

  // backward to manager
  ARM::AXI::Phase prev_phase = phase;
  tlm_sync_enum reply = mgr_tsockets[mgr_id]->nb_transport_bw(payload, phase);

  if (log_level <= LogLevel::DEBUG)
    print_payload(payload, prev_phase, reply, phase);

  // unlock channel if response sent
  if (is_r_valid_last(prev_phase) && is_r_ready(phase)) {
    auto &R = sub_state[sub_id].R;
    if (R.cur == &payload) {
      R = ChannelState{};
      payloads2sub.erase(&payload);
      payloads2mgr.erase(&payload);
    }
  }
  if (is_b_valid(prev_phase) && is_b_ready(phase)) {
    auto &W = sub_state[sub_id].W;
    if (W.cur == &payload) {
      W = ChannelState{};
      payloads2sub.erase(&payload);
      payloads2mgr.erase(&payload);
    }
  }

  return reply;
}

// -------------------------------------------------------
// helper functions
// -------------------------------------------------------
int AXIBus::route_payload(ARM::AXI::Payload &payload) {
  UserSignals user = UserSignals::decode(payload.user);
  return user.destination == chip_id ? 0 : 1;
}

// -------------------------------------------------------
// debug functions
// -------------------------------------------------------
void AXIBus::print_payload(ARM::AXI::Payload &payload, ARM::AXI::Phase phase,
                           tlm::tlm_sync_enum reply, ARM::AXI::Phase) {
  std::ostringstream stream;

  ARM::AXI::Command command = payload.get_command();

  const char *phase_name = "?";
  bool show_addr = true;
  bool show_data = false;
  bool show_resp = false;
  bool inc_beat = false;
  bool first_beat = false;
  bool last_beat = false;

  bool updated = reply == tlm::TLM_UPDATED;

  switch (phase) {
  case ARM::AXI::PHASE_UNINITIALIZED:
    phase_name = "PHASE_UNINITIALIZED";
    show_addr = false;
    break;
  case ARM::AXI::AW_VALID:
    phase_name = (updated ? "AW VALID READY" : "AW VALID -----");
    break;
  case ARM::AXI::AW_READY:
    phase_name = "AW ----- READY";
    break;
  case ARM::AXI::W_VALID:
  case ARM::AXI::W_VALID_LAST:
    phase_name = (updated ? "W  VALID READY" : "W  VALID -----");
    inc_beat = updated;
    show_data = true;
    first_beat = true;
    last_beat = updated;
    break;
  case ARM::AXI::W_READY:
    inc_beat = true;
    phase_name = "W  ----- READY";
    show_data = true;
    last_beat = true;
    break;
  case ARM::AXI::B_VALID:
    phase_name = (updated ? "B  VALID READY" : "B  VALID -----");
    show_resp = true;
    last_beat = updated;
    break;
  case ARM::AXI::B_READY:
    phase_name = "B  ----- READY";
    show_resp = true;
    last_beat = true;
    break;
  case ARM::AXI::AR_VALID:
    phase_name = (updated ? "AR VALID READY" : "AR VALID -----");
    break;
  case ARM::AXI::AR_READY:
    phase_name = "AR ----- READY";
    break;
  case ARM::AXI::R_VALID:
  case ARM::AXI::R_VALID_LAST:
    phase_name = (updated ? "R  VALID READY" : "R  VALID -----");
    inc_beat = updated;
    show_data = true;
    show_resp = true;
    first_beat = true;
    last_beat = updated;
    break;
  case ARM::AXI::R_READY:
    inc_beat = true;
    phase_name = "R  ----- READY";
    show_data = true;
    show_resp = true;
    last_beat = true;
    break;
  case ARM::AXI::AC_VALID:
    phase_name = (updated ? "AC VALID READY" : "AC VALID -----");
    break;
  case ARM::AXI::AC_READY:
    phase_name = "AC ----- READY";
    break;
  case ARM::AXI::CR_VALID:
    phase_name = (updated ? "CR VALID READY" : "CR VALID -----");
    break;
  case ARM::AXI::CR_READY:
    phase_name = "CR ----- READY";
    break;
  case ARM::AXI::CD_VALID:
  case ARM::AXI::CD_VALID_LAST:
    phase_name = (updated ? "CD VALID READY" : "CD VALID -----");
    inc_beat = updated;
    show_data = true;
    first_beat = true;
    last_beat = updated;
    break;
  case ARM::AXI::CD_READY:
    inc_beat = true;
    phase_name = "CD ----- READY";
    show_data = true;
    last_beat = true;
    break;
  case ARM::AXI::WACK:
    phase_name = "WACK";
    show_addr = false;
    break;
  case ARM::AXI::RACK:
    phase_name = "RACK";
    show_addr = false;
    break;
  default:
    show_addr = false;
    break;
  }

  // track beats
  if (first_beat &&
      payload_burst_index.find(&payload) == payload_burst_index.end())
    payload_burst_index[&payload] = 0;

  std::ostringstream _sc_stream;
  _sc_stream << std::left << std::setw(16) << sc_time_stamp()
             << " | \033[34m[DEBUG]\033[0m  | " << std::setw(32) << name()
             << " | ";

  // channel source/destination info
  auto mgr_it = payloads2mgr.find(&payload);
  auto sub_it = payloads2sub.find(&payload);
  if (mgr_it != payloads2mgr.end() && sub_it != payloads2sub.end()) {
    _sc_stream << "MGR[" << mgr_it->second << "] -> SUB[" << sub_it->second
               << "] | ";
  }

  // phase
  _sc_stream << phase_name << " ";

  if (show_addr) {
    _sc_stream << "@0x" << std::right << std::setw(8) << std::setfill('0')
               << std::hex << payload.get_address() << std::dec << ' ';

    if (command != ARM::AXI::COMMAND_SNOOP) {
      const static char *burst_types[] = {"FIXED", "INCR ", "WRAP "};
      ARM::AXI::Burst burst = payload.get_burst();

      _sc_stream << payload.get_beat_count() << "x"
                 << (8 * (1 << payload.get_size())) << "bits ";
      _sc_stream << (burst <= ARM::AXI::BURST_WRAP ? burst_types[burst]
                                                   : "?????")
                 << ' ';
    }
  }

  if (show_resp) {
    ARM::AXI::Resp resp = payload.get_resp();
    const static char *resp_types[] = {"OKAY  ", "EXOKAY", "SLVERR", "DECERR"};
    _sc_stream << (resp <= ARM::AXI::RESP_DECERR ? resp_types[resp] : "??????")
               << ' ';
  } else {
    _sc_stream << "       ";
  }

  if (show_data) {
    unsigned burst_index = payload_burst_index[&payload];
    _sc_stream << (burst_index == payload.get_len() ? "LAST " : "     ")
               << "DATA:";

    uint64_t byte_strobe(uint64_t(~0));
    switch (payload.get_command()) {
    case ARM::AXI::COMMAND_WRITE:
      payload.write_out_beat(burst_index, beat_data);
      byte_strobe = payload.write_out_beat_strobe(burst_index);
      break;
    case ARM::AXI::COMMAND_READ:
      payload.read_out_beat(burst_index, beat_data);
      break;
    case ARM::AXI::COMMAND_SNOOP:
      payload.snoop_out_beat(burst_index, beat_data);
      break;
    default:
      assert(0);
      break;
    }

    _sc_stream << std::uppercase << std::hex;
    unsigned size = 1 << payload.get_size();
    for (int i = size - 1; i >= 0; i--) {
      if ((byte_strobe >> (i % 8)) & 1)
        _sc_stream << std::setw(2) << std::setfill('0')
                   << unsigned(beat_data[i]);
      else
        _sc_stream << "XX";
      if (i != 0 && !(i % 8))
        _sc_stream << "_";
    }
    _sc_stream << std::dec;

    if (inc_beat)
      payload_burst_index[&payload] = burst_index + 1;
  }

  if (last_beat && payload_burst_index[&payload] == payload.get_beat_count())
    payload_burst_index.erase(&payload);

  std::cout << _sc_stream.str() << std::endl;
}