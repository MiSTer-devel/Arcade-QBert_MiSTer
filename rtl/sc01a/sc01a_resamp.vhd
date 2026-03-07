-- sc01a_resamp.vhd
-- Votrax SC01-A Output Resampler: variable rate → 48 kHz
--
-- Copyright (c) 2026 shufps
-- https://github.com/shufps/votrax-sc01a-vhdl
--
-- BSD 3-Clause License
--
-- Converts the SC01-A variable sample rate (~52 kHz, clk_dac-dependent)
-- to a fixed 48 kHz output using linear interpolation.
--
-- Key design decisions:
--   - phase_inc pre-computed as 256-entry LUT indexed by clk_dac
--     (avoids any runtime division)
--   - 48 kHz DDS increment derived from CLK_HZ generic
--   - Pipelined interpolation (2 stages) for timing closure
--
-- Developed with Claude (Anthropic): rubber duck without equal,
-- tireless code parrot, and sounding board for the great
-- "do we really need runtime division?" debate (we did not).

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sc01a_resamp is
    generic (
        CLK_HZ      : integer := 40_000_000;
        SAMPLE_BITS : integer := 16
    );
    port (
        clk : in std_logic;
        reset_n : in std_logic;
        s_in : in signed(SAMPLE_BITS-1 downto 0);
        s_valid : in std_logic;
        clk_dac : in std_logic_vector(7 downto 0); -- replaces speech_clock
        s_out : out signed(SAMPLE_BITS-1 downto 0);
        s_out_valid : out std_logic
    );
end entity;

architecture rtl of sc01a_resamp is

    -- 48kHz DDS: inc = 48000 * 2^32 / CLK_HZ
    constant INC_48K : unsigned(31 downto 0) :=
                                               to_unsigned(integer(48000.0 * 4294967296.0 / real(CLK_HZ)), 32);

    -- phase_inc LUT indexed by clk_dac (0..255)
    -- phase_inc = 864000 * 32768 / sc01_rate
    -- sc01_rate = speech_clock / 18
    -- speech_clock = 950000 + (dac - 0xA0) * 5500, clamped to dac >= 0x40
    type phase_inc_lut_t is array(0 to 255) of unsigned(15 downto 0);
    constant PHASE_INC_LUT : phase_inc_lut_t := (
        -- dac=0x00
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        -- dac=0x08
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        -- dac=0x10
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        -- dac=0x18
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        -- dac=0x20
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        -- dac=0x28
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        -- dac=0x30
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        -- dac=0x38
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65535, 16),
        -- dac=0x40
        to_unsigned(65535, 16), to_unsigned(65535, 16), to_unsigned(65376, 16), to_unsigned(64544, 16),
        to_unsigned(63744, 16), to_unsigned(62976, 16), to_unsigned(62208, 16), to_unsigned(61472, 16),
        -- dac=0x48
        to_unsigned(60736, 16), to_unsigned(60032, 16), to_unsigned(59328, 16), to_unsigned(58656, 16),
        to_unsigned(57984, 16), to_unsigned(57344, 16), to_unsigned(56736, 16), to_unsigned(56096, 16),
        -- dac=0x50
        to_unsigned(55488, 16), to_unsigned(54912, 16), to_unsigned(54336, 16), to_unsigned(53760, 16),
        to_unsigned(53216, 16), to_unsigned(52672, 16), to_unsigned(52128, 16), to_unsigned(51616, 16),
        -- dac=0x58
        to_unsigned(51072, 16), to_unsigned(50592, 16), to_unsigned(50080, 16), to_unsigned(49600, 16),
        to_unsigned(49152, 16), to_unsigned(48672, 16), to_unsigned(48224, 16), to_unsigned(47776, 16),
        -- dac=0x60
        to_unsigned(47328, 16), to_unsigned(46912, 16), to_unsigned(46464, 16), to_unsigned(46048, 16),
        to_unsigned(45632, 16), to_unsigned(45248, 16), to_unsigned(44864, 16), to_unsigned(44480, 16),
        -- dac=0x68
        to_unsigned(44096, 16), to_unsigned(43712, 16), to_unsigned(43328, 16), to_unsigned(42976, 16),
        to_unsigned(42624, 16), to_unsigned(42272, 16), to_unsigned(41920, 16), to_unsigned(41600, 16),
        -- dac=0x70
        to_unsigned(41248, 16), to_unsigned(40928, 16), to_unsigned(40608, 16), to_unsigned(40288, 16),
        to_unsigned(39968, 16), to_unsigned(39648, 16), to_unsigned(39360, 16), to_unsigned(39072, 16),
        -- dac=0x78
        to_unsigned(38752, 16), to_unsigned(38464, 16), to_unsigned(38176, 16), to_unsigned(37920, 16),
        to_unsigned(37632, 16), to_unsigned(37344, 16), to_unsigned(37088, 16), to_unsigned(36832, 16),
        -- dac=0x80
        to_unsigned(36576, 16), to_unsigned(36320, 16), to_unsigned(36064, 16), to_unsigned(35808, 16),
        to_unsigned(35552, 16), to_unsigned(35296, 16), to_unsigned(35072, 16), to_unsigned(34816, 16),
        -- dac=0x88
        to_unsigned(34592, 16), to_unsigned(34368, 16), to_unsigned(34144, 16), to_unsigned(33920, 16),
        to_unsigned(33696, 16), to_unsigned(33472, 16), to_unsigned(33248, 16), to_unsigned(33024, 16),
        -- dac=0x90
        to_unsigned(32832, 16), to_unsigned(32608, 16), to_unsigned(32416, 16), to_unsigned(32224, 16),
        to_unsigned(32000, 16), to_unsigned(31808, 16), to_unsigned(31616, 16), to_unsigned(31424, 16),
        -- dac=0x98
        to_unsigned(31232, 16), to_unsigned(31040, 16), to_unsigned(30848, 16), to_unsigned(30688, 16),
        to_unsigned(30496, 16), to_unsigned(30304, 16), to_unsigned(30144, 16), to_unsigned(29952, 16),
        -- dac=0xA0
        to_unsigned(29792, 16), to_unsigned(29600, 16), to_unsigned(29440, 16), to_unsigned(29280, 16),
        to_unsigned(29120, 16), to_unsigned(28960, 16), to_unsigned(28800, 16), to_unsigned(28640, 16),
        -- dac=0xA8
        to_unsigned(28480, 16), to_unsigned(28320, 16), to_unsigned(28160, 16), to_unsigned(28000, 16),
        to_unsigned(27840, 16), to_unsigned(27712, 16), to_unsigned(27552, 16), to_unsigned(27392, 16),
        -- dac=0xB0
        to_unsigned(27264, 16), to_unsigned(27104, 16), to_unsigned(26976, 16), to_unsigned(26848, 16),
        to_unsigned(26688, 16), to_unsigned(26560, 16), to_unsigned(26432, 16), to_unsigned(26272, 16),
        -- dac=0xB8
        to_unsigned(26144, 16), to_unsigned(26016, 16), to_unsigned(25888, 16), to_unsigned(25760, 16),
        to_unsigned(25632, 16), to_unsigned(25504, 16), to_unsigned(25376, 16), to_unsigned(25248, 16),
        -- dac=0xC0
        to_unsigned(25120, 16), to_unsigned(24992, 16), to_unsigned(24896, 16), to_unsigned(24768, 16),
        to_unsigned(24640, 16), to_unsigned(24544, 16), to_unsigned(24416, 16), to_unsigned(24288, 16),
        -- dac=0xC8
        to_unsigned(24192, 16), to_unsigned(24064, 16), to_unsigned(23968, 16), to_unsigned(23840, 16),
        to_unsigned(23744, 16), to_unsigned(23616, 16), to_unsigned(23520, 16), to_unsigned(23424, 16),
        -- dac=0xD0
        to_unsigned(23296, 16), to_unsigned(23200, 16), to_unsigned(23104, 16), to_unsigned(23008, 16),
        to_unsigned(22880, 16), to_unsigned(22784, 16), to_unsigned(22688, 16), to_unsigned(22592, 16),
        -- dac=0xD8
        to_unsigned(22496, 16), to_unsigned(22400, 16), to_unsigned(22304, 16), to_unsigned(22208, 16),
        to_unsigned(22112, 16), to_unsigned(22016, 16), to_unsigned(21920, 16), to_unsigned(21824, 16),
        -- dac=0xE0
        to_unsigned(21728, 16), to_unsigned(21632, 16), to_unsigned(21536, 16), to_unsigned(21472, 16),
        to_unsigned(21376, 16), to_unsigned(21280, 16), to_unsigned(21184, 16), to_unsigned(21120, 16),
        -- dac=0xE8
        to_unsigned(21024, 16), to_unsigned(20928, 16), to_unsigned(20832, 16), to_unsigned(20768, 16),
        to_unsigned(20672, 16), to_unsigned(20608, 16), to_unsigned(20512, 16), to_unsigned(20448, 16),
        -- dac=0xF0
        to_unsigned(20352, 16), to_unsigned(20256, 16), to_unsigned(20192, 16), to_unsigned(20128, 16),
        to_unsigned(20032, 16), to_unsigned(19968, 16), to_unsigned(19872, 16), to_unsigned(19808, 16),
        -- dac=0xF8
        to_unsigned(19712, 16), to_unsigned(19648, 16), to_unsigned(19584, 16), to_unsigned(19488, 16),
        to_unsigned(19424, 16), to_unsigned(19360, 16), to_unsigned(19296, 16), to_unsigned(19200, 16)
    );

    signal phase_48k : unsigned(32 downto 0) := (others => '0');
    signal tick_48k : std_logic := '0';

    signal sample_prev : signed(SAMPLE_BITS-1 downto 0) := (others => '0');
    signal sample_curr : signed(SAMPLE_BITS-1 downto 0) := (others => '0');

    signal interp_phase : unsigned(15 downto 0) := (others => '0');
    signal phase_inc : unsigned(15 downto 0) := to_unsigned(29792, 16);

    -- Pipeline stage 1 registers
    signal p1_diff : signed(SAMPLE_BITS downto 0) := (others => '0');
    signal p1_phase : unsigned(15 downto 0) := (others => '0');
    signal p1_sample_prev : signed(SAMPLE_BITS-1 downto 0) := (others => '0');
    signal p1_valid : std_logic := '0';

begin

    -- phase_inc from LUT (registered, 1 cycle latency - fine since dac changes slowly)
    process (clk)
    begin
        if rising_edge(clk) then
            if reset_n = '0' then
                phase_inc <= to_unsigned(29792, 16);
            else
                phase_inc <= PHASE_INC_LUT(to_integer(unsigned(clk_dac)));
            end if;
        end if;
    end process;

    -- 48kHz DDS
    process (clk)
        variable sum : unsigned(32 downto 0);
    begin
        if rising_edge(clk) then
            if reset_n = '0' then
                phase_48k <= (others => '0');
                tick_48k <= '0';
            else
                sum := ('0' & phase_48k(31 downto 0)) + ('0' & INC_48K);
                phase_48k <= sum;
                tick_48k <= sum(32);
            end if;
        end if;
    end process;

    -- Sample FIFO + interp_phase + Pipeline Stage 1
    process (clk)
        variable next_phase : unsigned(15 downto 0);
    begin
        if rising_edge(clk) then
            if reset_n = '0' then
                sample_prev <= (others => '0');
                sample_curr <= (others => '0');
                interp_phase <= (others => '0');
                p1_diff <= (others => '0');
                p1_phase <= (others => '0');
                p1_sample_prev <= (others => '0');
                p1_valid <= '0';
            else
                next_phase := interp_phase;
                p1_valid <= '0';

                if s_valid = '1' then
                    sample_prev <= sample_curr;
                    sample_curr <= s_in;
                    if next_phase >= 32768 then
                        next_phase := next_phase - 32768;
                    else
                        next_phase := (others => '0');
                    end if;
                end if;

                if tick_48k = '1' then
                    p1_diff <= resize(sample_curr, SAMPLE_BITS+1) - resize(sample_prev, SAMPLE_BITS+1);
                    p1_phase <= next_phase;
                    p1_sample_prev <= sample_prev;
                    p1_valid <= '1';
                    next_phase := next_phase + phase_inc;
                end if;

                interp_phase <= next_phase;
            end if;
        end if;
    end process;

    -- Pipeline Stage 2: multiply + add → output
    -- p1_diff is SAMPLE_BITS+1 bits, p1_phase is 17 bits (signed) → product is SAMPLE_BITS+18 bits
    -- p1_phase represents fraction in Q15 (0..32767), so shift right 15 to get SAMPLE_BITS bits
    process (clk)
        variable interp : signed(SAMPLE_BITS+17 downto 0);
        variable result : signed(SAMPLE_BITS-1 downto 0);
    begin
        if rising_edge(clk) then
            if reset_n = '0' then
                s_out <= (others => '0');
                s_out_valid <= '0';
            else
                s_out_valid <= '0';
                if p1_valid = '1' then
                    interp := p1_diff * signed('0' & p1_phase);
                    result := p1_sample_prev + interp(SAMPLE_BITS+14 downto 15);
                    s_out <= result;
                    s_out_valid <= '1';
                end if;
            end if;
        end if;
    end process;

end architecture;
