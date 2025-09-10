#include "modules/interconnects/serial_link/NetworkLayer.h"

#include "ARM/TLM/arm_axi4.h"
#include "logging.h"

SLNetworkLayer::SLNetworkLayer(sc_module_name name, unsigned axi_width,
                               int num_credits)
    : sc_module(name), axi_width(axi_width), num_credits(num_credits),
      force_send_thresh(num_credits - 4),
      axi_in("axi_in", *this, &SLNetworkLayer::nb_transport_fw,
             ARM::TLM::PROTOCOL_AXI4, axi_width) {
  // Initial values
  committer_state_q.write(Idle);
  entropy_q.write(false);
  axis_reg_valid_in.write(false);
  axis_reg_ready_in.write(false);
  axis_reg_valid_out.write(false);
  axis_reg_ready_out.write(false);
  credits_out_q.write(num_credits);
  credits_to_send_q.write(0);
  credit_to_send_force.write(false);

  SC_METHOD(clk_posedge);
  dont_initialize();
  sensitive << clk.pos();

  SC_THREAD(committer_thread);
  sensitive << fw_event << aw_gnt << ar_gnt << axi_in_sig << axi_out_sig
            << committer_state_q;
  async_reset_signal_is(rst_n, false);

  SC_THREAD(sender_thread);
  sensitive << fw_event << aw_gnt << w_gnt << b_gnt << ar_gnt << r_gnt
            << axis_reg_valid_in << axis_reg_ready_in;
  async_reset_signal_is(rst_n, false);

  SC_THREAD(flow_control_thread);
  async_reset_signal_is(rst_n, false);
}

// -------------------------------------------------------
// Committer
// -------------------------------------------------------
void SLNetworkLayer::committer_thread() {
  while (true) {
    wait();

    aw_gnt.write(false);
    w_gnt.write(false);
    b_gnt.write(false);
    ar_gnt.write(false);
    r_gnt.write(false);
    committer_state_d.write(committer_state_q);

    switch (committer_state_q.read()) {
    case Idle:
      if (is_aw_valid(axi_in_trans.req_phase)) {
        aw_gnt.write(true);
        SC_LOG_DEBUG(this, "COMMITTER | Granting AW");
      }
      if (is_ar_valid(axi_in_trans.req_phase)) {
        ar_gnt.write(true);
        SC_LOG_DEBUG(this, "COMMITTER | Granting AR");
      }

      if (!ar_gnt.read() && !aw_gnt.read() &&
          is_r_valid(axi_out_trans.rsp_phase)) {
        r_gnt.write(true);
        SC_LOG_DEBUG(this, "COMMITTER | Granting R");
      }

      if (aw_gnt.read() && is_aw_ready(axi_in_trans.rsp_phase)) {
        committer_state_d.write(AwPend);
        SC_LOG_DEBUG(this, "COMMITTER | Transition: Idle -> AwPend");
      }
      if (ar_gnt.read() && is_ar_ready(axi_in_trans.rsp_phase)) {
        committer_state_d.write(ArPend);
        SC_LOG_DEBUG(this, "COMMITTER | Transition: Idle -> ArPend");
      }
      break;

    case AwPend:
      if (is_ar_valid(axi_in_trans.req_phase)) {
        ar_gnt.write(true);
        SC_LOG_DEBUG(this, "COMMITTER | Granting AR (while AW pending)");
      } else {
        if (is_r_valid(axi_out_trans.rsp_phase)) {
          r_gnt.write(true);
          SC_LOG_DEBUG(this, "COMMITTER | Granting R");
        }
        if (is_w_valid(axi_in_trans.req_phase)) {
          w_gnt.write(true);
          SC_LOG_DEBUG(this, "COMMITTER | Granting W");
        }
        if (is_w_valid_last(axi_in_trans.req_phase)) {
          w_gnt.write(true);
          SC_LOG_DEBUG(this, "COMMITTER | Granting W");
        }
      }

      if (is_w_valid_last(axi_in_trans.req_phase) &&
          is_w_ready(axi_in_trans.rsp_phase)) {
        committer_state_d.write(
            (ar_gnt && is_ar_ready(axi_in_trans.req_phase)) ? ArPend : Idle);
        SC_LOG_DEBUG(this,
                     "COMMITTER | AW burst done, transitioning to "
                         << ((ar_gnt && is_ar_ready(axi_in_trans.req_phase))
                                 ? "ArPend"
                                 : "Idle"));
      } else {
        committer_state_d.write((ar_gnt && is_ar_ready(axi_in_trans.req_phase))
                                    ? ArAwPend
                                    : AwPend);
      }
      break;

    case ArPend:
      if (is_aw_valid(axi_in_trans.req_phase)) {
        aw_gnt.write(true);
        SC_LOG_DEBUG(this, "COMMITTER | Granting AW (while AR pending)");
      } else {
        if (is_r_valid(axi_out_trans.rsp_phase)) {
          r_gnt.write(true);
          SC_LOG_DEBUG(this, "COMMITTER | Granting R");
        }
        if (is_w_valid(axi_in_trans.req_phase)) {
          w_gnt.write(true);
          SC_LOG_DEBUG(this, "COMMITTER | Granting W");
        }
      }

      if (is_r_valid_last(axi_in_trans.req_phase) &&
          is_r_ready(axi_in_trans.rsp_phase)) {
        committer_state_d.write(
            (aw_gnt && is_aw_ready(axi_in_trans.req_phase)) ? AwPend : Idle);
        SC_LOG_DEBUG(this,
                     "COMMITTER | AR burst done, transitioning to "
                         << ((aw_gnt && is_aw_ready(axi_in_trans.req_phase))
                                 ? "AwPend"
                                 : "Idle"));
      } else {
        committer_state_d.write((aw_gnt && is_aw_ready(axi_in_trans.req_phase))
                                    ? ArAwPend
                                    : ArPend);
      }
      break;

    case ArAwPend: {
      if (is_r_valid(axi_out_trans.req_phase)) {
        r_gnt.write(true);
        SC_LOG_DEBUG(this, "COMMITTER | Granting R");
      }
      if (is_w_valid(axi_in_trans.req_phase)) {
        w_gnt.write(true);
        SC_LOG_DEBUG(this, "COMMITTER | Granting W");
      }

      bool aw_pend_idle = is_r_valid_last(axi_in_trans.req_phase) &&
                          is_r_ready(axi_in_trans.rsp_phase);
      bool ar_pend_idle = is_w_valid_last(axi_in_trans.req_phase) &&
                          is_w_ready(axi_in_trans.rsp_phase);

      if (aw_pend_idle & ar_pend_idle) {
        committer_state_d.write(Idle);
        SC_LOG_DEBUG(this, "COMMITTER | Both AR/AW pending done, back to Idle");
      } else if (aw_pend_idle) {
        committer_state_d.write(AwPend);
        SC_LOG_DEBUG(this, "COMMITTER | AW pending done, stay in AwPend");
      } else if (ar_pend_idle) {
        committer_state_d.write(ArPend);
        SC_LOG_DEBUG(this, "COMMITTER | AR pending done, stay in ArPend");
      }
      break;
    }

    default:
      break;
    }

    b_gnt.write(is_b_valid(axi_out_trans.req_phase));
    if (is_b_valid(axi_out_trans.req_phase)) {
      SC_LOG_DEBUG(this, "COMMITTER | Granting B");
    }
  }
}

// -------------------------------------------------------
// Sender
// -------------------------------------------------------
void SLNetworkLayer::sender_thread() {
  while (true) {
    wait();

    updated = true;

    payload_out = new Payload_t(axi_width);
    payload_out->credit = credits_to_send_q;

    if (aw_gnt.read()) {
      payload_out->axi_ch.addr = axi_in_trans.w_payload->get_address();
      payload_out->hdr = TagAW;
      SC_LOG_DEBUG(this,
                   "SENDER | Sending AW, addr=" << payload_out->axi_ch.addr);
    } else if (w_gnt.read()) {
      axi_in_trans.w_payload->write_out_beat(axi_in_trans.w_beat_count,
                                             payload_out->axi_ch.data.data());
      payload_out->hdr = TagW;
      SC_LOG_DEBUG(this,
                   "SENDER | Sending W beat #" << axi_in_trans.w_beat_count);
    } else if (ar_gnt.read()) {
      payload_out->axi_ch.addr = axi_in_trans.r_payload->get_address();
      payload_out->hdr = TagAR;
      SC_LOG_DEBUG(this,
                   "SENDER | Sending AR, addr=" << payload_out->axi_ch.addr);
    } else if (r_gnt.read()) {
      axi_out_trans.r_payload->read_out_beat(axi_out_trans.r_beat_count,
                                             payload_out->axi_ch.data.data());
      payload_out->hdr = TagR;
      SC_LOG_DEBUG(this,
                   "SENDER | Sending R beat #" << axi_out_trans.r_beat_count);
    }

    if (b_gnt.read()) {
      payload_out->b_valid = true;
      payload_out->b = axi_out_trans.w_payload->get_resp();
      SC_LOG_DEBUG(this,
                   "SENDER | Sending B response, resp=" << payload_out->b);
    }

    axis_reg_valid_in.write((payload_out->hdr != TagIdle) ||
                            payload_out->b_valid ||
                            credit_to_send_force.read());

    if (credits_out_q.read() == 0) {
      axis_reg_valid_in.write(false);
      SC_LOG_DEBUG(this, "SENDER | Blocked: credits_out_q==0");
    } else if (credits_out_q.read() == 1 && credits_to_send_q.read() == 0) {
      axis_reg_valid_in.write(false);
      SC_LOG_DEBUG(this,
                   "SENDER | Blocked: credits_out_q==1, no credits to return");
    }

    axis_reg_ready_in.write((stream_fifo_out.num_free() > 0));
    SC_LOG_DEBUG(this, "SENDER | axis_reg_ready_in="
                           << axis_reg_ready_in.read()
                           << " free=" << stream_fifo_out.num_free());

    if (axis_reg_ready_in.read() && axis_reg_valid_in.read()) {
      if (aw_gnt.read()) {
        axi_in_trans.rsp_phase = ARM::AXI::AW_READY;
        SC_LOG_DEBUG(this, "SENDER | AW_READY");
      }
      if (w_gnt.read()) {
        axi_in_trans.rsp_phase = ARM::AXI::W_READY;
        SC_LOG_DEBUG(this, "SENDER | W_READY");
      }
      if (b_gnt.read()) {
        SC_LOG_DEBUG(this, "SENDER | B_READY (stubbed)");
      }
      if (ar_gnt.read()) {
        axi_in_trans.rsp_phase = ARM::AXI::AR_READY;
        SC_LOG_DEBUG(this, "SENDER | AR_READY");
      }
      if (r_gnt.read()) {
        SC_LOG_DEBUG(this, "SENDER | R_READY (stubbed)");
      }
    } else {
      axi_in_trans.rsp_phase = ARM::AXI::PHASE_UNINITIALIZED;
      SC_LOG_DEBUG(this, "SENDER | PHASE_UNINITIALIZED");
    }

    axi_in_sig.write(axi_in_trans);
    axi_out_sig.write(axi_out_trans);
  }
}

// -------------------------------------------------------
// Flow control
// -------------------------------------------------------
void SLNetworkLayer::flow_control_thread() {
  while (true) {
    wait();

    SC_LOG_DEBUG(this, "Flow control tick");

    credits_out_d.write(credits_out_q);
    credits_to_send_d.write(credits_to_send_q);
    credit_to_send_force.write(false);

    SC_LOG_DEBUG(this, "Initial state: credits_out_q="
                           << credits_out_q.read()
                           << " credits_to_send_q=" << credits_to_send_q.read()
                           << " force_send_thresh=" << force_send_thresh);

    // Send empty packets with credits if there are too many
    // credits to send but no AXI request transaction
    if (credits_to_send_q >= force_send_thresh) {
      credit_to_send_force.write(true);
      SC_LOG_DEBUG(this, "Force sending credits (credits_to_send_q="
                             << credits_to_send_q.read() << ")");
    }

    if (axis_reg_valid_in && axis_reg_ready_in) {
      unsigned old_credits = credits_out_d.read();
      credits_out_d.write(old_credits - 1);
      credits_to_send_d.write(0);

      SC_LOG_DEBUG(
          this,
          "AXIS transaction accepted: axis_reg_valid_in=1 axis_reg_ready_in=1");
      SC_LOG_DEBUG(this, "Credits decremented: " << old_credits << " -> "
                                                 << credits_out_d.read());
      SC_LOG_DEBUG(this, "Credits to send reset to 0");
    }

    // TODO: Increment credits with incoming payloads
    SC_LOG_DEBUG(this, "End of cycle: credits_out_d="
                           << credits_out_d.read()
                           << " credits_to_send_d=" << credits_to_send_d.read()
                           << " credit_to_send_force="
                           << credit_to_send_force.read());
  }
}

void SLNetworkLayer::clk_posedge() {
  committer_state_q.write(committer_state_d);
  // TODO: Introduce some randomness
  entropy_q.write(entropy_d);

  // credits_out_q.write(credits_out_d);
  credits_to_send_q.write(credits_to_send_d);

  ARM::AXI::Phase phase;

  if (updated && axis_reg_ready_in.read() && axis_reg_valid_in.read()) {
    phase = axi_in_trans.rsp_phase;
    ARM::AXI::Payload *payload_ptr = nullptr;
    if (is_aw_ready(phase)) {
      payload_ptr = axi_in_trans.w_payload;
    } else if (is_w_ready(phase)) {
      payload_ptr = axi_in_trans.w_payload;
      axi_in_trans.w_beat_count += 1;
    }
    axi_in.nb_transport_bw(*payload_ptr, phase);
    stream_fifo_out.write(payload_out);
    SC_LOG_DEBUG(this, "Payload written to FIFO");
  }

  updated = false;
}

// -------------------------------------------------------
// Transport functions
// -------------------------------------------------------
tlm_sync_enum SLNetworkLayer::nb_transport_fw(ARM::AXI::Payload &payload,
                                              ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AW_VALID:
    axi_in_trans.w_payload = &payload;
    axi_in_trans.req_phase = phase;
    payload.ref();
    break;
  case ARM::AXI::W_VALID:
    axi_in_trans.req_phase = phase;
    break;
  case ARM::AXI::W_VALID_LAST:
    axi_in_trans.req_phase = phase;
    break;
  case ARM::AXI::AR_VALID:
    axi_in_trans.r_payload = &payload;
    axi_in_trans.req_phase = phase;
    payload.ref();
    break;
  default:
    SC_REPORT_ERROR(name(), "AXI TLM Protocol: Unrecognized phase");
    break;
  }

  fw_event.notify(SC_ZERO_TIME);
  return TLM_ACCEPTED;
}