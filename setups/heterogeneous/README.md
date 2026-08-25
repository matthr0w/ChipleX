## Heterogeneous System

This setup streams data through a heterogeneous multi-chiplet pipeline: an IO chiplet feeds two compute chiplets, each with a DFP and a generic accelerator plus its own memory chiplet, connected over serial-link and SPI interconnects. Data is fetched from memory, processed by the accelerators, and passed back.
