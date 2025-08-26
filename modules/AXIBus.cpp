#include "AXIBus.h"

#include "include/logging.h"

AXIBus::AXIBus(sc_module_name name, unsigned int chip_id,
               unsigned int num_managers, unsigned int num_subordinates)
    : sc_module(name), chip_id(chip_id) {
  sub_state.resize(num_subordinates);

  mgr_tsockets.reserve(num_managers);
  for (unsigned i = 0; i < num_managers; ++i) {
    std::ostringstream nm;
    nm << "targ_" << i;
    mgr_tsockets.emplace_back(
        std::make_unique<ARM::AXI4::SimpleTargetSocketTagged<AXIBus>>(
            nm.str().c_str(), *this, i, &AXIBus::nb_transport_fw,
            ARM::TLM::PROTOCOL_AXI4, 32));
  }

  sub_isockets.reserve(num_subordinates);
  for (unsigned i = 0; i < num_subordinates; ++i) {
    std::ostringstream nm;
    nm << "init_" << i;
    sub_isockets.emplace_back(
        std::make_unique<ARM::AXI4::SimpleInitiatorSocketTagged<AXIBus>>(
            nm.str().c_str(), *this, i, &AXIBus::nb_transport_bw,
            ARM::TLM::PROTOCOL_AXI4, 32));
  }
}

// -------------------------------------------------------
// transport functions
// -------------------------------------------------------
tlm_sync_enum AXIBus::nb_transport_fw(int mgr_id, ARM::AXI::Payload &payload,
                                      ARM::AXI::Phase &phase) {
  std::scoped_lock lock(fw_mutex);

  SC_LOG_DEBUG_NO_TX(this, "AXI TLM Protocol: " << phase_to_string(phase));

  int sid = 0;
  if (auto it = payloads2sub.find(&payload); it != payloads2sub.end())
    sid = it->second;
  else {
    sid = route(payload); // currently returns 0
    payloads2sub[&payload] = sid;
    payloads2mgr[&payload] = (int)mgr_id;
  }

  SubState &S = sub_state[sid];

  // ---- READ CHANNEL ARBITRATION ----
  if (is_ar_valid(phase)) {
    if (!S.R.busy) {
      // lock read channel for this tx
      S.R.busy = true;
      S.R.cur = &payload;
      S.R.mgr = (int)mgr_id;
      S.R.completion_mark = false;
    } else if (S.R.cur != &payload) {
      // serialize: another read in progress on this subordinate
      // keep phase unchanged: the manager will retry next cycle
      return TLM_ACCEPTED;
    }
  }

  // completion of read when BW told us R_VALID_LAST, and now FW sees R_READY
  if (S.R.busy && S.R.cur == &payload && is_r_ready(phase) &&
      S.R.completion_mark) {
    S.R = ChanState{};
    // on full completion we can also drop mappings if there is no write using
    // same tx
    payloads2sub.erase(&payload);
    payloads2mgr.erase(&payload);
  }

  // ---- WRITE CHANNEL ARBITRATION ----
  if (is_aw_valid(phase)) {
    if (!S.W.busy) {
      // lock write channel for this tx
      S.W.busy = true;
      S.W.cur = &payload;
      S.W.mgr = (int)mgr_id;
      S.W.completion_mark = false;
    } else if (S.W.cur != &payload) {
      // serialize: another write in progress on this subordinate
      // keep phase unchanged: the manager will retry next cycle
      return TLM_ACCEPTED;
    }
  }

  // write data beats can only flow for the locked write tx
  if (is_w_valid(phase) || is_w_valid_last(phase)) {
    if (!S.W.busy || S.W.cur != &payload) {
      return TLM_ACCEPTED; // stall W beats until AW locked this channel
    }
  }

  // completion of write when BW told us B_VALID, and now FW sees B_READY
  if (S.W.busy && S.W.cur == &payload && is_b_ready(phase) &&
      S.W.completion_mark) {
    S.W = ChanState{};
    payloads2sub.erase(&payload);
    payloads2mgr.erase(&payload);
  }

  // forward to subordinate
  return sub_isockets[sid]->nb_transport_fw(payload, phase);
}

tlm_sync_enum AXIBus::nb_transport_bw(int sub_id, ARM::AXI::Payload &payload,
                                      ARM::AXI::Phase &phase) {
  SC_LOG_DEBUG_NO_TX(this, "AXI TLM Protocol: " << phase_to_string(phase));

  std::scoped_lock lock(bw_mutex);

  // mark completions when we see them from the subordinate:
  if (is_r_valid_last(phase)) {
    auto &R = sub_state[sub_id].R;
    if (R.cur == &payload)
      R.completion_mark = true;
  }
  if (is_b_valid(phase)) {
    auto &W = sub_state[sub_id].W;
    if (W.cur == &payload)
      W.completion_mark = true;
  }

  // find manager for this tx
  int mgr = 0;
  if (auto it = payloads2mgr.find(&payload); it != payloads2mgr.end())
    mgr = it->second;

  // backward to manager
  return mgr_tsockets[mgr]->nb_transport_bw(payload, phase);
}