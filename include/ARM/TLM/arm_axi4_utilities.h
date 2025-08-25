#include "arm_axi4_phase.h"

inline const char *phase_to_string(ARM::AXI::Phase phase) {
  switch (phase) {
  case ARM::AXI4::PHASE_UNINITIALIZED:
    return "PHASE_UNINITIALIZED";
  case ARM::AXI4::AW_VALID:
    return "AW_VALID";
  case ARM::AXI4::AW_READY:
    return "AW_READY";
  case ARM::AXI4::W_VALID:
    return "W_VALID";
  case ARM::AXI4::W_VALID_LAST:
    return "W_VALID_LAST";
  case ARM::AXI4::W_READY:
    return "W_READY";
  case ARM::AXI4::B_VALID:
    return "B_VALID";
  case ARM::AXI4::B_VALID_PERSIST:
    return "B_VALID_PERSIST";
  case ARM::AXI4::B_VALID_COMP_PERSIST:
    return "B_VALID_COMP_PERSIST";
  case ARM::AXI4::B_VALID_TAGMATCH:
    return "B_VALID_TAGMATCH";
  case ARM::AXI4::B_VALID_COMP_TAGMATCH:
    return "B_VALID_COMP_TAGMATCH";
  case ARM::AXI4::B_READY:
    return "B_READY";
  case ARM::AXI4::AR_VALID:
    return "AR_VALID";
  case ARM::AXI4::AR_READY:
    return "AR_READY";
  case ARM::AXI4::R_VALID:
    return "R_VALID";
  case ARM::AXI4::R_VALID_LAST:
    return "R_VALID_LAST";
  case ARM::AXI4::R_READY:
    return "R_READY";
  case ARM::AXI4::AC_VALID:
    return "AC_VALID";
  case ARM::AXI4::AC_READY:
    return "AC_READY";
  case ARM::AXI4::CR_VALID:
    return "CR_VALID";
  case ARM::AXI4::CR_READY:
    return "CR_READY";
  case ARM::AXI4::CD_VALID:
    return "CD_VALID";
  case ARM::AXI4::CD_VALID_LAST:
    return "CD_VALID_LAST";
  case ARM::AXI4::CD_READY:
    return "CD_READY";
  case ARM::AXI4::WACK:
    return "WACK";
  case ARM::AXI4::RACK:
    return "RACK";
  case ARM::AXI4::WQOSACCEPT:
    return "WQOSACCEPT";
  case ARM::AXI4::RQOSACCEPT:
    return "RQOSACCEPT";
  default:
    return "UNKNOWN_PHASE";
  }
}