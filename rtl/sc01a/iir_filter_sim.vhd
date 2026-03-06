library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity iir_filter_sim is
    generic (
        N_X : integer := 4;
        N_Y : integer := 4;
        FP_FRAC : integer := 15
    );
    port (
        clk     : in  std_logic;
        reset_n : in  std_logic;

        start   : in  std_logic;
        x_in    : in  signed(17 downto 0);

        -- ROM passed in as 8 coefficients (0..7), s(2.15)
        rom0 : in signed(17 downto 0);
        rom1 : in signed(17 downto 0);
        rom2 : in signed(17 downto 0);
        rom3 : in signed(17 downto 0);
        rom4 : in signed(17 downto 0);
        rom5 : in signed(17 downto 0);
        rom6 : in signed(17 downto 0);
        rom7 : in signed(17 downto 0);

        y_out : out signed(17 downto 0);
        done  : out std_logic
    );
end entity;

architecture sim of iir_filter_sim is
    type hist_t is array(natural range <>) of signed(17 downto 0);
    signal x_hist : hist_t(0 to N_X-1) := (others => (others => '0'));
    signal y_hist : hist_t(0 to N_Y-1) := (others => (others => '0'));

    type rom_t is array(0 to 7) of signed(17 downto 0);
    signal rom : rom_t;

    -- helper: arithmetic shift right for signed integer64-ish
    function asr64(x : signed(63 downto 0); sh : integer) return signed is
        variable r : signed(63 downto 0);
    begin
        -- numeric_std shift_right on signed is arithmetic
        r := shift_right(x, sh);
        return r;
    end function;

begin
    -- pack ROM inputs into an array for easy indexing
    rom(0) <= rom0; rom(1) <= rom1; rom(2) <= rom2; rom(3) <= rom3;
    rom(4) <= rom4; rom(5) <= rom5; rom(6) <= rom6; rom(7) <= rom7;

    process(clk)
        variable acc  : signed(63 downto 0) := (others => '0');
        variable prod : signed(63 downto 0) := (others => '0');
        variable res64: signed(63 downto 0) := (others => '0');
        variable result18 : signed(17 downto 0) := (others => '0');
    begin
        if rising_edge(clk) then
            if reset_n = '0' then
                x_hist <= (others => (others => '0'));
                y_hist <= (others => (others => '0'));
                y_out  <= (others => '0');
                done   <= '0';
            else


                if start = '1' then
                    -- shift x history
                    for i in N_X-1 downto 1 loop
                        x_hist(i) <= x_hist(i-1);
                    end loop;
                    x_hist(0) <= x_in;

                    -- compute using old histories (like typical synchronous step)
                    acc := (others => '0');

                    -- Feedforward: acc += x[i] * a[i]
                    for i in 0 to N_X-1 loop
                        -- rom[1+i] is a[i]
                        prod := resize(x_hist(i) * rom(1+i), 64);
                        acc := acc + prod;
                    end loop;

                    -- Feedback: acc -= y[i] * b[i+1]
                    for i in 0 to (N_Y-2) loop
                        -- rom[5+i] is b[i+1]
                        prod := resize(y_hist(i) * rom(5+i), 64);
                        acc := acc - prod;
                    end loop;

                    -- >> FP_FRAC
                    res64 := asr64(acc, FP_FRAC);

                    -- take low 18 bits (like C++ int32 cast after shift)
                    result18 := res64(17 downto 0);

                    -- shift y history and output
                    for i in N_Y-1 downto 1 loop
                        y_hist(i) <= y_hist(i-1);
                    end loop;
                    y_hist(0) <= result18;

                    y_out <= result18;
                    done  <= '1';
                end if;
            end if;
        end if;
    end process;

end architecture;