-- sc01a_filter.vhd
-- Votrax SC01-A Filter Pipeline Sequencer
--
-- Copyright (c) 2026 shufps
-- https://github.com/shufps/votrax-sc01a-vhdl
--
-- BSD 3-Clause License
--
-- Based on analog_calc() from votrax.cpp (MAME)
-- Copyright (c) Olivier Galibert
--
-- Chains 6 iir_filter_slow instances; F1 and FN run in parallel.
-- Total sequencer latency: ~54 cycles
-- Budget at 50 MHz / 52 kHz ≈ 961 cycles -- plenty of headroom.
--
-- Developed with Claude (Anthropic): rubber duck without equal,
-- tireless code parrot, and occasional voice of reason when
-- filter poles went wandering into instability.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sc01a_filter is
    generic (
        SIM_FILTER        : boolean := false; -- true = instant filter, false = real BRAM filter
        ENABLE_F2N        : boolean := false  -- true = F2N injection filter active
    );

    port (
        clk : in std_logic;
        reset_n : in std_logic;

        -- Trigger: one pulse per sample (from DDS sclock)
        start : in std_logic;

        -- Filter ROM addresses (from filters_commit)
        f1_addr : in unsigned(6 downto 0);
        f2v_addr : in unsigned(11 downto 0);
        f3_addr : in unsigned(6 downto 0);
        -- f4, fx, fn: constant base = 0

        -- Sample inputs (from chip_update)
        filt_va : in unsigned(3 downto 0);
        filt_fa : in unsigned(3 downto 0);
        filt_fc : in unsigned(3 downto 0);
        pitch : in unsigned(7 downto 0);
        closure : in unsigned(4 downto 0); -- 5-bit (0..28)
        cur_noise : in std_logic;

        -- ROM interfaces
        rom_addr_f1 : out unsigned(6 downto 0);
        rom_data_f1 : in signed(17 downto 0);
        rom_addr_f2v : out unsigned(11 downto 0);
        rom_data_f2v : in signed(17 downto 0);
        rom_addr_f3 : out unsigned(6 downto 0);
        rom_data_f3 : in signed(17 downto 0);
        rom_addr_f4 : out unsigned(2 downto 0);
        rom_data_f4 : in signed(17 downto 0);
        rom_addr_fx : out unsigned(2 downto 0);
        rom_data_fx : in signed(17 downto 0);
        rom_addr_fn : out unsigned(2 downto 0);
        rom_data_fn : in signed(17 downto 0);
        rom_addr_f2n : out unsigned(11 downto 0);
        rom_data_f2n : in signed(17 downto 0);

        -- Output
        sample_out : out signed(17 downto 0);
        done : out std_logic
    );
end entity;

architecture rtl of sc01a_filter is

    -- Glottal wave table s(2.15)
    type glottal_t is array(0 to 8) of signed(17 downto 0);
    constant GLOTTAL : glottal_t := (
        to_signed(0, 18),
        to_signed(-18725, 18),
        to_signed(32767, 18),
        to_signed(28101, 18),
        to_signed(23405, 18),
        to_signed(18725, 18),
        to_signed(14050, 18),
        to_signed(9362, 18),
        to_signed(4681, 18)
    );

    function fp_scale15(val : signed(17 downto 0); vol : unsigned(3 downto 0))
        return signed is
        variable step1 : signed(22 downto 0); -- 18 + 4 + 1 sign = 23-bit
        variable step2 : signed(40 downto 0); -- 23 + 18 = 41-bit
    begin
        step1 := val * signed(resize(vol, 5)); -- 18*5-bit
        step2 := step1 * to_signed(2185, 18);
        return step2(32 downto 15); -- >> 15, keep 18-bit
    end function;

    function fp_scale7(val : signed(17 downto 0); clos : unsigned(2 downto 0))
        return signed is
        variable step1 : signed(21 downto 0); -- 18 + 3 + 1 sign
        variable step2 : signed(39 downto 0); -- 22 + 18
    begin
        step1 := val * signed(resize(clos, 4));
        step2 := step1 * to_signed(4681, 18);
        return step2(32 downto 15);
    end function;

    -- Per-filter signals
    type filt_sig_t is record
        start : std_logic;
        x_in : signed(17 downto 0);
        rom_addr : unsigned(2 downto 0);
        rom_data : signed(17 downto 0);
        y_out : signed(17 downto 0);
        done : std_logic;
    end record;

    signal f1 : filt_sig_t;
    signal f2v : filt_sig_t;
    signal f2n : filt_sig_t;
    signal fn : filt_sig_t;
    signal f3 : filt_sig_t;
    signal f4 : filt_sig_t;
    signal fx : filt_sig_t;

    -- Sequencer FSM
    type state_t is (
        S_IDLE,
        S_GLOTTAL,
        S_WAIT_F1,
        S_WAIT_F2V,
        S_WAIT_FN,
        S_SCALE_FC,
        S_WAIT_F2N,
        S_WAIT_F3,
        S_NOISE_INJ,
        S_START_F4,
        S_WAIT_F4,
        S_CLOSURE,
        S_START_FX,
        S_WAIT_FX,
        S_DONE
    );
    signal state : state_t := S_IDLE;

    signal v_sig : signed(17 downto 0) := (others => '0');
    signal n_sig : signed(17 downto 0) := (others => '0');
    signal vn_sig : signed(17 downto 0) := (others => '0');

begin

    -- ================================================================
    -- IIR filter instances
    -- ================================================================
    u_f1 : entity work.iir_filter_slow
        generic map(N_X => 4, N_Y => 4)
        port map(
            clk => clk, reset_n => reset_n,
            start => f1.start, x_in => f1.x_in,
            rom_addr => f1.rom_addr, rom_data => f1.rom_data,
            y_out => f1.y_out, done => f1.done);

    u_f2v : entity work.iir_filter_slow
        generic map(N_X => 4, N_Y => 4)
        port map(
            clk => clk, reset_n => reset_n,
            start => f2v.start, x_in => f2v.x_in,
            rom_addr => f2v.rom_addr, rom_data => f2v.rom_data,
            y_out => f2v.y_out, done => f2v.done);

    u_fn : entity work.iir_filter_slow
        generic map(N_X => 4, N_Y => 4)
        port map(
            clk => clk, reset_n => reset_n,
            start => fn.start, x_in => fn.x_in,
            rom_addr => fn.rom_addr, rom_data => fn.rom_data,
            y_out => fn.y_out, done => fn.done);

    u_f3 : entity work.iir_filter_slow
        generic map(N_X => 4, N_Y => 4)
        port map(
            clk => clk, reset_n => reset_n,
            start => f3.start, x_in => f3.x_in,
            rom_addr => f3.rom_addr, rom_data => f3.rom_data,
            y_out => f3.y_out, done => f3.done);

    u_f4 : entity work.iir_filter_slow
        generic map(N_X => 4, N_Y => 4)
        port map(
            clk => clk, reset_n => reset_n,
            start => f4.start, x_in => f4.x_in,
            rom_addr => f4.rom_addr, rom_data => f4.rom_data,
            y_out => f4.y_out, done => f4.done);

    u_f2n : entity work.iir_filter_slow
        generic map(N_X => 2, N_Y => 2)
        port map(
            clk => clk, reset_n => reset_n,
            start => f2n.start, x_in => f2n.x_in,
            rom_addr => f2n.rom_addr, rom_data => f2n.rom_data,
            y_out => f2n.y_out, done => f2n.done);

    u_fx : entity work.iir_filter_slow
        generic map(N_X => 2, N_Y => 2)
        port map(
            clk => clk, reset_n => reset_n,
            start => fx.start, x_in => fx.x_in,
            rom_addr => fx.rom_addr, rom_data => fx.rom_data,
            y_out => fx.y_out, done => fx.done);

    -- ================================================================
    -- ROM address wiring: filter base addr OR coeff_idx from iir_filter
    -- ================================================================
    rom_addr_f1 <= f1_addr or resize(f1.rom_addr, 7);
    rom_addr_f2v <= f2v_addr or resize(f2v.rom_addr, 12);
    rom_addr_f3 <= f3_addr or resize(f3.rom_addr, 7);
    rom_addr_f4 <= f4.rom_addr;
    rom_addr_fx <= fx.rom_addr;
    rom_addr_fn <= fn.rom_addr;
    rom_addr_f2n <= f2v_addr or resize(f2n.rom_addr, 12);
    f1.rom_data <= rom_data_f1;
    f2v.rom_data <= rom_data_f2v;
    fn.rom_data <= rom_data_fn;
    f2n.rom_data <= rom_data_f2n;
    f3.rom_data <= rom_data_f3;
    f4.rom_data <= rom_data_f4;
    fx.rom_data <= rom_data_fx;

    -- ================================================================
    -- Sequencer FSM
    -- ================================================================
    process (clk)
        variable noise_inp : signed(17 downto 0);
        variable clos_val : unsigned(2 downto 0);
        variable noise_scale : integer range 0 to 20 := 5;
        variable tmp : signed(63 downto 0);
    begin
        if rising_edge(clk) then
            if reset_n = '0' then
                state <= S_IDLE;
                done <= '0';
                f1.start <= '0';
                f2v.start <= '0';
                f2n.start <= '0';
                fn.start <= '0';
                f3.start <= '0';
                f4.start <= '0';
                fx.start <= '0';
            else
                -- Default: clear start pulses
                f1.start <= '0';
                f2v.start <= '0';
                f2n.start <= '0';
                fn.start <= '0';
                f3.start <= '0';
                f4.start <= '0';
                fx.start <= '0';
                done <= '0';

                case state is

                    when S_IDLE =>
                        if start = '1' then
                            state <= S_GLOTTAL;
                        end if;

                        -- ------------------------------------------------
                        -- Compute scaled inputs, fire F1 and FN in parallel
                        -- ------------------------------------------------
                    when S_GLOTTAL =>

                        if pitch >= to_unsigned(9 * 8, 8) then
                            v_sig <= (others => '0');
                            f1.x_in <= (others => '0');
                        else
                            v_sig <= fp_scale15(GLOTTAL(to_integer(pitch(7 downto 3))), filt_va);
                            f1.x_in <= fp_scale15(GLOTTAL(to_integer(pitch(7 downto 3))), filt_va);
                        end if;

                        if pitch(6) = '1' and cur_noise = '1' then
                            noise_inp := to_signed(16384, 18);
                        else
                            noise_inp := to_signed(-16384, 18);
                        end if;
                        n_sig <= fp_scale15(noise_inp, filt_fa);
                        fn.x_in <= fp_scale15(noise_inp, filt_fa);

                        f1.start <= '1';
                        fn.start <= '1';
                        state <= S_WAIT_F1;

                        -- ------------------------------------------------
                        -- F1 done → fire F2V; FN runs in parallel
                        -- ------------------------------------------------
                    when S_WAIT_F1 =>
                        if f1.done = '1' then
                            f2v.x_in <= f1.y_out;
                            f2v.start <= '1';
                            state <= S_WAIT_F2V;
                        end if;

                        -- ------------------------------------------------
                        -- F2V done → save v_sig; wait for FN if needed
                        -- ------------------------------------------------
                    when S_WAIT_F2V =>
                        if f2v.done = '1' then
                            v_sig <= f2v.y_out;
                            if fn.done = '1' then
                                n_sig <= fn.y_out;
                                state <= S_SCALE_FC;
                            else
                                state <= S_WAIT_FN;
                            end if;
                        end if;

                    when S_WAIT_FN =>
                        if fn.done = '1' then
                            n_sig <= fn.y_out;
                            state <= S_SCALE_FC;
                        end if;

                        -- ------------------------------------------------
                        -- Scale noise by filt_fc → fire F2N (if enabled)
                        -- or pass v_sig directly to F3
                        -- ------------------------------------------------
                    when S_SCALE_FC =>
                        if ENABLE_F2N then
                            f2n.x_in <= fp_scale15(n_sig, filt_fc);
                            f2n.start <= '1';
                            state <= S_WAIT_F2N;
                        else
                            f3.x_in <= v_sig;
                            f3.start <= '1';
                            state <= S_WAIT_F3;
                        end if;

                        -- ------------------------------------------------
                        -- F2N done → mix v + f2n, fire F3
                        -- ------------------------------------------------
                    when S_WAIT_F2N =>
                        if f2n.done = '1' then
                            f3.x_in <= v_sig + f2n.y_out;
                            f3.start <= '1';
                            state <= S_WAIT_F3;
                        end if;

                        -- ------------------------------------------------
                        -- F3 done → noise injection
                        -- ------------------------------------------------
                    when S_WAIT_F3 =>
                        if f3.done = '1' then
                            vn_sig <= f3.y_out;
                            state <= S_NOISE_INJ;
                        end if;

                        -- ------------------------------------------------
                        -- Second noise injection:
                        -- vn += n * (5 + (15 xor filt_fc)) / 20
                        -- Two-step to avoid triple multiply:
                        --   tmp = n * noise_scale  (>> nothing yet)
                        --   vn += tmp * 1638 >> 15
                        -- ------------------------------------------------
                    when S_NOISE_INJ =>
                        noise_scale := 5 + to_integer(to_unsigned(15, 4) xor filt_fc);
                        tmp := resize(n_sig * to_signed(noise_scale, 18), 64);
                        -- tmp(32:15) is n*noise_scale in s(2.15), then *1638>>15
                        vn_sig <= vn_sig + signed(resize(
                                  shift_right(tmp(32 downto 0) * to_signed(1638, 18), 15),
                                  18));
                        state <= S_START_F4;

                    when S_START_F4 =>
                        f4.x_in <= vn_sig;
                        f4.start <= '1';
                        state <= S_WAIT_F4;

                    when S_WAIT_F4 =>
                        if f4.done = '1' then
                            vn_sig <= f4.y_out;
                            state <= S_CLOSURE;
                        end if;

                        -- ------------------------------------------------
                        -- Closure amplitude: vn * (7 xor closure(4:2)) / 7
                        -- ------------------------------------------------
                    when S_CLOSURE =>
                        vn_sig <= fp_scale7(vn_sig,
                                  unsigned(closure(4 downto 2)) xor "111");
                        state <= S_START_FX;

                    when S_START_FX =>
                        fx.x_in <= vn_sig;
                        fx.start <= '1';
                        state <= S_WAIT_FX;

                    when S_WAIT_FX =>
                        if fx.done = '1' then
                            sample_out <= fx.y_out;
                            done <= '1';
                            state <= S_DONE;
                        end if;

                    when S_DONE =>
                        state <= S_IDLE;

                    when others =>
                        state <= S_IDLE;

                end case;
            end if;
        end if;
    end process;

end architecture;
