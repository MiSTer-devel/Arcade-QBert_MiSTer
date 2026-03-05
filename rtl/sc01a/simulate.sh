#!/bin/bash
set -e
ghdl -a --std=08 votrax_tb_vectors.vhd sc01a_rom.vhd \
        f1_rom.vhd f2v_rom.vhd f3_rom.vhd f4_rom.vhd fx_rom.vhd fn_rom.vhd \
        iir_filter_sim.vhd sc01a_resamp.vhd iir_filter_slow.vhd sc01a_filter.vhd sc01a.vhd sc01a_tb.vhd
ghdl -e --std=08 sc01a_tb
ghdl -r sc01a_tb --stop-time=20280ms --vcd=wave.vcd
