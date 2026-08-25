# Serial-link model attribution

The serial-link interconnect modeled here (network layer, data-link layer,
channel allocator, credit-based flow control, and stream FIFOs) is an original
SystemC implementation whose architecture is inspired by the open-source
serial_link project from the PULP platform:

  https://github.com/pulp-platform/serial_link

That project provides a SystemVerilog RTL implementation and is distributed
under the Solderpad Hardware License, version 0.51 (SHL-0.51). No source from
that project is copied into this repository; only the architecture and protocol
concepts are reused. This SystemC model is covered by the ChipleX license (see
LICENSE at the repository root).
