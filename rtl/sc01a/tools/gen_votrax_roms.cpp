#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cfloat>
#include <initializer_list>
#include <unistd.h>

// T = 2 * sclock / cclock = 2 * (main/18) / (main/36) = 4 (fixed, clock-independent)
static constexpr double T  = 4.0;
static constexpr double T2 = 16.0;

// ============================================================
// Fixed-point format: s(2.15) = 18-bit signed
// range: -2.0 ... +1.99997
// scale factor: 2^15 = 32768
// ============================================================
static constexpr int    FP_BITS  = 18;
static constexpr int    FP_FRAC  = 15;
static constexpr double FP_SCALE = (1 << FP_FRAC); // 32768.0
static constexpr int    FP_MAX   =  (1 << (FP_BITS-1)) - 1; //  131071
static constexpr int    FP_MIN   = -(1 << (FP_BITS-1));     // -131072

int to_fixed(double v)
{
    int r = (int)round(v * FP_SCALE);
    if (r > FP_MAX) { fprintf(stderr, "OVERFLOW: %f -> %d\n", v, r); r = FP_MAX; }
    if (r < FP_MIN) { fprintf(stderr, "UNDERFLOW: %f -> %d\n", v, r); r = FP_MIN; }
    return r;
}

double bits_to_caps(uint32_t value, std::initializer_list<double> caps)
{
    double total = 0.0;
    for (double c : caps) {
        if (value & 1) total += c;
        value >>= 1;
    }
    return total;
}

// ============================================================
// Filter builders (clock-free, normalized by b0)
// ============================================================

// ROM layout per filter setting (8 slots, coeff_idx = lower 3 bits of addr):
//   idx 0 → b0 = 1.0 always (stored anyway for uniformity)
//   idx 1 → a0
//   idx 2 → a1
//   idx 3 → a2
//   idx 4 → a3
//   idx 5 → b1
//   idx 6 → b2
//   idx 7 → b3

struct Coeffs {
    double b0, a0, a1, a2, a3, b1, b2, b3;
    double operator[](int i) const {
        const double *p = &b0;
        return p[i];
    }
};

Coeffs build_standard(double c1t, double c1b, double c2t, double c2b,
                       double c3, double c4)
{
    double k0 = c1t / c1b;
    double k1 = c4 * c2t / (c1b * c3);
    double k2 = c4 * c2b / (c1b * c3);

    double m0 = T  * k0;
    double m1 = T  * k1;
    double m2 = T2 * k2;

    double b0 = 1+m1+m2;
    return {
        1.0,
        (1+m0)/b0, (3+m0)/b0, (3-m0)/b0, (1-m0)/b0,
        (3+m1-m2)/b0, (3-m1-m2)/b0, (1-m1+m2)/b0
    };
}

// Lowpass: a1=a2=a3=0, b2=b3=0 → pad with 0 for uniform ROM
Coeffs build_lowpass(double c1t, double c1b)
{
    double k  = (c1b / c1t) * (150.0/4000.0);
    double m  = T * k;
    double b0 = 1.0 + m;
    return { 1.0, 1.0/b0, 0, 0, 0, (1.0-m)/b0, 0, 0 };
}

// Noise shaper: a1=0, a2=-a0, b3=0 → pad with 0 for uniform ROM
Coeffs build_noise_shaper(double c1, double c2t, double c2b,
                           double c3, double c4)
{
    double k0 = c2t * c3 * c2b / c4;
    double k1 = c2t * c2b;
    double k2 = c1  * c2t * c3 / c4;

    double m0 = T * k0;
    double m1 = T * k1;
    double m2 = T * k2;

    double b0 = 1+m1+m2;
    return { 1.0, m0/b0, 0, -m0/b0, 0, (2-2*m2)/b0, (1-m1+m2)/b0, 0 };
}

// ============================================================
// Sanity check: all coefficients fit in s(2.15)?
// ============================================================
void sanity_check()
{
    double gmax = -DBL_MAX, gmin = DBL_MAX;
    auto check = [&](const Coeffs &c) {
        for (int i = 0; i < 8; i++) {
            if (c[i] > gmax) gmax = c[i];
            if (c[i] < gmin) gmin = c[i];
        }
    };

    for (uint32_t b = 0; b < 16; b++)
        check(build_standard(11247, 11797, 949, 52067,
              2280 + bits_to_caps(b, {2546, 4973, 9861, 19724}), 166272));

    for (uint32_t qb = 0; qb < 16; qb++) {
        double c2t = 829 + bits_to_caps(qb, {1390, 2965, 5875, 11297});
        for (uint32_t fb = 0; fb < 32; fb++)
            check(build_standard(24840, 29154, c2t, 38180,
                  2352 + bits_to_caps(fb, {833, 1663, 3164, 6327, 12654}), 34270));
    }

    for (uint32_t b = 0; b < 16; b++)
        check(build_standard(0, 17594, 868, 18828,
              8480 + bits_to_caps(b, {2226, 4485, 9056, 18111}), 50019));

    check(build_standard(0, 28810, 1165, 21457, 8558, 7289));
    check(build_lowpass(1122, 23131));
    check(build_noise_shaper(15500, 14854, 8450, 9523, 14083));

    fprintf(stderr, "Global coeff range: [%f, %f]\n", gmin, gmax);
    fprintf(stderr, "s(2.15) range:      [%f, %f]\n",
            (double)FP_MIN/FP_SCALE, (double)FP_MAX/FP_SCALE);
    fprintf(stderr, "Fits: %s\n\n",
            (gmin >= (double)FP_MIN/FP_SCALE && gmax <= (double)FP_MAX/FP_SCALE)
            ? "YES" : "NO - OVERFLOW!");
}

// ============================================================
// VHDL output helpers
// ============================================================

static const char *COEFF_NAMES[8] = {"b0","a0","a1","a2","a3","b1","b2","b3"};

void print_vhdl_entry(const Coeffs &c, const char *comment, bool last_entry)
{
    printf("    -- %s\n", comment);
    for (int i = 0; i < 8; i++) {
        int v = to_fixed(c[i]);
        printf("    \"");
        for (int b = FP_BITS-1; b >= 0; b--)
            printf("%d", (v >> b) & 1);
        bool last_word = (i == 7) && last_entry;
        printf("\"%s  -- [%d] %s = %+.6f\n",
               last_word ? " " : ",", i, COEFF_NAMES[i], c[i]);
    }
    if (!last_entry) printf("\n");
}

// ============================================================
// C header output (for MAME fixed-point test)
// ============================================================
void gen_c_header(FILE *f)
{
    fprintf(f, "// Auto-generated by gen_votrax_roms.cpp\n");
    fprintf(f, "// SC01A filter coefficient ROMs, s(2.15) fixed-point\n");
    fprintf(f, "// scale = 2^15 = 32768\n");
    fprintf(f, "//\n");
    fprintf(f, "// addr = (filter_param << 3) | coeff_idx\n");
    fprintf(f, "// coeff_idx: 0=b0(=32768) 1=a0 2=a1 3=a2 4=a3 5=b1 6=b2 7=b3\n");
    fprintf(f, "\n");
    fprintf(f, "#pragma once\n");
    fprintf(f, "#include <cstdint>\n\n");
    fprintf(f, "static constexpr int VOTRAX_FP_SCALE = %d; // 2^%d\n\n",
            (int)FP_SCALE, FP_FRAC);

    auto write_rom = [&](const char *name, const char *comment,
                         int entries, int32_t *data) {
        fprintf(f, "// %s\n", comment);
        fprintf(f, "static const int32_t %s[%d] = {\n", name, entries * 8);
        for (int e = 0; e < entries; e++) {
            fprintf(f, "    ");
            for (int i = 0; i < 8; i++) {
                fprintf(f, "%7d", data[e*8+i]);
                if (e*8+i < entries*8-1) fprintf(f, ",");
            }
            fprintf(f, "  // [%d]\n", e);
        }
        fprintf(f, "};\n\n");
    };

    // Helper to fill array from Coeffs
    auto fill = [](int32_t *dst, const Coeffs &c) {
        for (int i = 0; i < 8; i++) dst[i] = to_fixed(c[i]);
    };

    // F1
    {
        int32_t data[16*8];
        for (uint32_t b = 0; b < 16; b++) {
            double c3 = 2280 + bits_to_caps(b, {2546, 4973, 9861, 19724});
            fill(&data[b*8], build_standard(11247, 11797, 949, 52067, c3, 166272));
        }
        fprintf(f, "// F1: 16 settings × 8 coeffs\n");
        fprintf(f, "// addr = (filt_f1 << 3) | coeff_idx\n");
        fprintf(f, "static const int32_t f1_rom[128] = {\n");
        for (int i = 0; i < 128; i++) {
            fprintf(f, "    %7d%s  // [%d] %s\n",
                    data[i], i<127?",":"",
                    i/8*8, COEFF_NAMES[i%8]);
        }
        fprintf(f, "};\n\n");
    }

    // F2V
    {
        fprintf(f, "// F2V: 512 settings × 8 coeffs = 4096 entries\n");
        fprintf(f, "// addr = (filt_f2q << 8) | (filt_f2 << 3) | coeff_idx\n");
        fprintf(f, "static const int32_t f2v_rom[4096] = {\n");
        for (uint32_t qb = 0; qb < 16; qb++) {
            double c2t = 829 + bits_to_caps(qb, {1390, 2965, 5875, 11297});
            for (uint32_t fb = 0; fb < 32; fb++) {
                double c3 = 2352 + bits_to_caps(fb, {833, 1663, 3164, 6327, 12654});
                int32_t entry[8];
                fill(entry, build_standard(24840, 29154, c2t, 38180, c3, 34270));
                int base = (qb*32+fb)*8;
                fprintf(f, "    ");
                for (int i = 0; i < 8; i++)
                    fprintf(f, "%7d%s", entry[i], (base+i < 4095) ? "," : " ");
                fprintf(f, "  // q=%u f=%u\n", qb, fb);
            }
        }
        fprintf(f, "};\n\n");
    }

    // F3
    {
        fprintf(f, "// F3: 16 settings × 8 coeffs\n");
        fprintf(f, "// addr = (filt_f3 << 3) | coeff_idx\n");
        fprintf(f, "static const int32_t f3_rom[128] = {\n");
        for (uint32_t b = 0; b < 16; b++) {
            double c3 = 8480 + bits_to_caps(b, {2226, 4485, 9056, 18111});
            int32_t entry[8];
            fill(entry, build_standard(0, 17594, 868, 18828, c3, 50019));
            fprintf(f, "    ");
            for (int i = 0; i < 8; i++)
                fprintf(f, "%7d%s", entry[i], (b*8+i < 127) ? "," : " ");
            fprintf(f, "  // [%u]\n", b);
        }
        fprintf(f, "};\n\n");
    }

    // F4, FX, FN
    auto write_const = [&](const char *name, const char *comment, const Coeffs &c) {
        int32_t entry[8];
        fill(entry, c);
        fprintf(f, "// %s\n", comment);
        fprintf(f, "static const int32_t %s[8] = {", name);
        for (int i = 0; i < 8; i++)
            fprintf(f, " %7d%s", entry[i], i<7?",":"");
        fprintf(f, " };\n\n");
    };

    write_const("f4_rom", "F4: constant",
                build_standard(0, 28810, 1165, 21457, 8558, 7289));
    write_const("fx_rom", "FX lowpass: constant",
                build_lowpass(1122, 23131));
    write_const("fn_rom", "FN noise shaper: constant",
                build_noise_shaper(15500, 14854, 8450, 9523, 14083));
}

// ============================================================
// VHDL ROM entity generator
// One entity per ROM, synchronous read for BRAM inference
// ============================================================

// Write one VHDL ROM entity to a file
// name:       entity name, e.g. "f1_rom"
// addr_bits:  address width (7 for 128 entries, 12 for 4096)
// depth:      number of entries
// data:       flat array of int32_t values (depth entries)
void gen_vhdl_rom_entity(FILE *f, const char *name, int addr_bits,
                          int depth, const int32_t *data)
{
    fprintf(f, "-- Auto-generated by gen_votrax_roms.cpp\n");
    fprintf(f, "-- %s: %d entries x 18-bit s(2.15)\n", name, depth);
    fprintf(f, "-- addr(2:0) = coeff_idx: 0=b0 1=a0 2=a1 3=a2 4=a3 5=b1 6=b2 7=b3\n");
    fprintf(f, "-- Synchronous read: 1-cycle latency, infers as BRAM\n");
    fprintf(f, "\n");
    fprintf(f, "library ieee;\n");
    fprintf(f, "use ieee.std_logic_1164.all;\n");
    fprintf(f, "use ieee.numeric_std.all;\n");
    fprintf(f, "\n");
    fprintf(f, "entity %s is\n", name);
    fprintf(f, "    port (\n");
    fprintf(f, "        clk  : in  std_logic;\n");
    fprintf(f, "        addr : in  unsigned(%d downto 0);\n", addr_bits - 1);
    fprintf(f, "        data : out signed(17 downto 0)\n");
    fprintf(f, "    );\n");
    fprintf(f, "end entity;\n");
    fprintf(f, "\n");
    fprintf(f, "architecture rtl of %s is\n", name);
    fprintf(f, "\n");
    fprintf(f, "    type rom_t is array(0 to %d) of signed(17 downto 0);\n", depth - 1);
    fprintf(f, "    constant ROM : rom_t := (\n");

    for (int i = 0; i < depth; i++) {
        int v = data[i];
        // print as 18-bit signed integer for VHDL to_signed()
        bool last = (i == depth - 1);
        // also print which coeff_idx this is
        const char *coeff_names[8] = {"b0","a0","a1","a2","a3","b1","b2","b3"};
        fprintf(f, "        to_signed(%7d, 18)%s  -- [%4d] %s = %+.6f\n",
                v, last ? " " : ",",
                i, coeff_names[i % 8], v / FP_SCALE);
    }

    fprintf(f, "    );\n");
    fprintf(f, "\n");
    fprintf(f, "begin\n");
    fprintf(f, "\n");
    fprintf(f, "    process(clk)\n");
    fprintf(f, "    begin\n");
    fprintf(f, "        if rising_edge(clk) then\n");
    fprintf(f, "            data <= ROM(to_integer(addr));\n");
    fprintf(f, "        end if;\n");
    fprintf(f, "    end process;\n");
    fprintf(f, "\n");
    fprintf(f, "end architecture;\n");
}

void gen_vhdl_rom_entities(const char *outdir)
{
    char path[256];
    auto fill = [](int32_t *dst, const Coeffs &c) {
        for (int i = 0; i < 8; i++) dst[i] = to_fixed(c[i]);
    };

    // ---- F1: 128 entries, 7-bit addr ----
    {
        int32_t data[128];
        for (uint32_t b = 0; b < 16; b++) {
            double c3 = 2280 + bits_to_caps(b, {2546, 4973, 9861, 19724});
            fill(&data[b*8], build_standard(11247, 11797, 949, 52067, c3, 166272));
        }
        snprintf(path, sizeof(path), "%s/f1_rom.vhd", outdir);
        FILE *f = fopen(path, "w");
        gen_vhdl_rom_entity(f, "f1_rom", 7, 128, data);
        fclose(f);
        fprintf(stderr, "Written: %s\n", path);
    }

    // ---- F2V: 4096 entries, 12-bit addr (full BRAM!) ----
    {
        int32_t data[4096];
        for (uint32_t qb = 0; qb < 16; qb++) {
            double c2t = 829 + bits_to_caps(qb, {1390, 2965, 5875, 11297});
            for (uint32_t fb = 0; fb < 32; fb++) {
                double c3 = 2352 + bits_to_caps(fb, {833, 1663, 3164, 6327, 12654});
                fill(&data[(qb*32+fb)*8],
                     build_standard(24840, 29154, c2t, 38180, c3, 34270));
            }
        }
        snprintf(path, sizeof(path), "%s/f2v_rom.vhd", outdir);
        FILE *f = fopen(path, "w");
        gen_vhdl_rom_entity(f, "f2v_rom", 12, 4096, data);
        fclose(f);
        fprintf(stderr, "Written: %s\n", path);
    }

    // ---- F3: 128 entries, 7-bit addr ----
    {
        int32_t data[128];
        for (uint32_t b = 0; b < 16; b++) {
            double c3 = 8480 + bits_to_caps(b, {2226, 4485, 9056, 18111});
            fill(&data[b*8], build_standard(0, 17594, 868, 18828, c3, 50019));
        }
        snprintf(path, sizeof(path), "%s/f3_rom.vhd", outdir);
        FILE *f = fopen(path, "w");
        gen_vhdl_rom_entity(f, "f3_rom", 7, 128, data);
        fclose(f);
        fprintf(stderr, "Written: %s\n", path);
    }

    // ---- F4: 8 entries, 3-bit addr (constant) ----
    {
        int32_t data[8];
        fill(data, build_standard(0, 28810, 1165, 21457, 8558, 7289));
        snprintf(path, sizeof(path), "%s/f4_rom.vhd", outdir);
        FILE *f = fopen(path, "w");
        gen_vhdl_rom_entity(f, "f4_rom", 3, 8, data);
        fclose(f);
        fprintf(stderr, "Written: %s\n", path);
    }

    // ---- FX: 8 entries, 3-bit addr (constant) ----
    {
        int32_t data[8];
        fill(data, build_lowpass(1122, 23131));
        snprintf(path, sizeof(path), "%s/fx_rom.vhd", outdir);
        FILE *f = fopen(path, "w");
        gen_vhdl_rom_entity(f, "fx_rom", 3, 8, data);
        fclose(f);
        fprintf(stderr, "Written: %s\n", path);
    }

    // ---- FN: 8 entries, 3-bit addr (constant) ----
    {
        int32_t data[8];
        fill(data, build_noise_shaper(15500, 14854, 8450, 9523, 14083));
        snprintf(path, sizeof(path), "%s/fn_rom.vhd", outdir);
        FILE *f = fopen(path, "w");
        gen_vhdl_rom_entity(f, "fn_rom", 3, 8, data);
        fclose(f);
        fprintf(stderr, "Written: %s\n", path);
    }
}

// ============================================================
int main()
{
    sanity_check();

    FILE *f = fopen("/tmp/votrax_rom_tables.h", "w");
    gen_c_header(f);
    fclose(f);
    fprintf(stderr, "C header: /tmp/votrax_rom_tables.h\n");

    gen_vhdl_rom_entities("/tmp");

    return 0;
}
