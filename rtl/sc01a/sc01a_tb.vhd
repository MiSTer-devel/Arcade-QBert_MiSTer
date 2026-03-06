-- sc01a_tb.vhd
-- GHDL testbench for sc01a
-- Plays the Q*bert "hello I'm Q*bert" phone sequence
-- Writes raw signed 16-bit samples (one per line as integer) to audio_out.raw
--
-- Compile & run:
--   ghdl -a votrax_tb_vectors.vhd sc01a_rom.vhd f1_rom.vhd f2v_rom.vhd
--         f3_rom.vhd f4_rom.vhd fx_rom.vhd fn_rom.vhd
--         iir_filter.vhd sc01a_filter.vhd sc01a.vhd sc01a_tb.vhd
--   ghdl -e sc01a_tb
--   ghdl -r sc01a_tb --stop-time=2000ms
--
-- Convert to WAV (Python):
--   import numpy as np, scipy.io.wavfile as wav
--   s = np.loadtxt("audio_out.raw", dtype=np.int16)
--   wav.write("audio_out.wav", 52778, s)

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use std.env.stop;

use work.votrax_tb_vectors.all;

entity sc01a_tb is
    generic (
        SKIP_START : integer := 3;
        SKIP_END : integer := 29
    );
end entity;

architecture sim of sc01a_tb is
    -- DUT signals
    signal clk : std_logic := '0';
    signal reset_n : std_logic := '0';
    signal p : std_logic_vector(5 downto 0) := (others => '0');
    signal inflection : std_logic_vector(1 downto 0) := (others => '0');
    signal stb : std_logic := '0';
    signal ar : std_logic;
    signal clk_dac : std_logic_vector(7 downto 0) := x"A0"; -- 950kHz
    signal audio_out : signed(15 downto 0);
    signal audio_valid : std_logic;
    constant CLK_PERIOD : time := 25 ns; -- 40 MHz

    file audio_file : text;

begin

    -- ================================================================
    -- DUT
    -- ================================================================
    u_dut : entity work.sc01a
        generic map(CLK_HZ => 40000000)
        port map(
            clk => clk,
            reset_n => reset_n,
            p => p,
            inflection => inflection,
            stb => stb,
            ar => ar,
            clk_dac => clk_dac,
            audio_out => audio_out,
            audio_valid => audio_valid
        );

    -- ================================================================
    -- Clock
    -- ================================================================
    clk <= not clk after CLK_PERIOD / 2;

    -- ================================================================
    -- Stimulus
    -- ================================================================
    process
        variable phone : integer;
        variable infl : integer;
        variable prev_ts_us : integer := 0;
        variable dac_val : integer;
        variable skipped : boolean := false;

    begin
        file_open(audio_file, "audio_out.raw", write_mode);

        -- Reset
        reset_n <= '0';
        wait for 10 * CLK_PERIOD;
        reset_n <= '1';
        wait for 10 * CLK_PERIOD;

        -- Replay events from votrax_tb_vectors
        for i in 0 to N_EVENTS - 1 loop

            if i < SKIP_START or i >= SKIP_END or true then
                -- Wait until event timestamp (relative to previous event)
                if INPUT_VECTORS(i).ts_us > prev_ts_us and not skipped then
                    wait for (INPUT_VECTORS(i).ts_us - prev_ts_us) * 1 us;
                end if;
                skipped := false;
                prev_ts_us := INPUT_VECTORS(i).ts_us;

                case INPUT_VECTORS(i).event is

                    when EV_RESET =>
                        reset_n <= '0';
                        wait for 10 * CLK_PERIOD;
                        reset_n <= '1';

                    when EV_CLOCK =>
                        dac_val := (INPUT_VECTORS(i).data - 950000) / 5500 + 16#A0#;
                        report "Clock " & integer'image(INPUT_VECTORS(i).data) & " DAC " & integer'image(dac_val) severity note;
                        if dac_val < 16#40# then
                            dac_val := 16#40#;
                        end if;
                        if dac_val > 16#FF# then
                            dac_val := 16#FF#;
                        end if;
                        clk_dac <= std_logic_vector(to_unsigned(dac_val, 8));

                    when EV_PHONE =>
                        phone := INPUT_VECTORS(i).data mod 64;
                        infl := (INPUT_VECTORS(i).data / 64) mod 4;
                        report "Phone " & integer'image(i) & "/" & integer'image(N_EVENTS - 1)
                            & " : " & integer'image(phone)
                            & " @ " & time'image(now)
                            severity note;
                        p <= std_logic_vector(to_unsigned(phone, 6));
                        inflection <= std_logic_vector(to_unsigned(infl, 2));
                        stb <= '1';
                        wait for 2 * CLK_PERIOD;
                        stb <= '0';

                    when EV_INFLECTION =>
                        inflection <= std_logic_vector(
                                      to_unsigned(INPUT_VECTORS(i).data, 2));

                    when others => null;

                end case;
            else
                report "skipped: " & integer'image(i) severity note;
                prev_ts_us := INPUT_VECTORS(i).ts_us;
                skipped := true;
            end if;

        end loop;

        -- Wait for last phoneme to finish
        wait for 500 ms;

        file_close(audio_file);
        report "Done! Samples written to audio_out.raw" severity note;
        report "Convert with: python3 raw_to_wav.py audio_out.raw audio_out.wav" severity note;
        stop;
    end process;

    -- ================================================================
    -- Audio capture: write one sample per filt_done pulse
    -- filt_done is a 1-cycle pulse at the true sample rate (~52778 Hz)
    -- This correctly captures every sample, including repeated values
    -- ================================================================
    process (clk)
        variable l : line;
        variable sample : integer;
    begin
        if rising_edge(clk) then
            if reset_n = '1' and audio_valid = '1' then
                sample := to_integer(signed(audio_out));
                write(l, integer'image(sample));
                writeline(audio_file, l);
            end if;
        end if;
    end process;

end architecture;