#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cfloat>
#include <initializer_list>
#include <complex>
#include <unistd.h>

// sclock/cclock = (main/18)/(main/36) = 2 (fixed, clock-independent)
static constexpr double SCLOCK_NORM = 2.0;

// Prewarped bilinear transform warp factor.
// fpeak_norm is the filter peak frequency in units of cclock (dimensionless).
// Returns zc_norm = zc/cclock, which replaces T=4 in the standard transform.
// At very low frequencies: zc_norm → 2*SCLOCK_NORM = T=4 (standard bilinear).
static double prewarp_zc(double fpeak_norm) {
    if (fpeak_norm < 1e-9) return 2.0 * SCLOCK_NORM; // fallback: standard bilinear
    double arg = M_PI * fpeak_norm / SCLOCK_NORM;
    if (arg >= M_PI / 2.0) return 2.0 * SCLOCK_NORM; // above Nyquist: fallback
    return 2.0 * M_PI * fpeak_norm / tan(arg);
}

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

    // Prewarped bilinear: fpeak_norm = peak frequency in units of cclock.
    // Formula derived from MAME build_standard_filter() (Galibert), adapted to
    // dimensionless cap ratios (cclock cancels out in the ratio).
    double fpeak_norm = (k2 > 1e-30) ? sqrt(fabs(k0*k1 - k2)) / (2*M_PI*k2) : 0.0;
    double zn  = prewarp_zc(fpeak_norm);
    double zn2 = zn * zn;

    double m0 = zn  * k0;
    double m1 = zn  * k1;
    double m2 = zn2 * k2;

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
    // fpeak_norm = cutoff frequency in units of cclock (cclock cancels)
    double fpeak_norm = (k > 1e-30) ? 1.0 / (2*M_PI*k) : 0.0;
    double zn = prewarp_zc(fpeak_norm);
    double m  = zn * k;
    double b0 = 1.0 + m;
    return { 1.0, 1.0/b0, 0, 0, 0, (1.0-m)/b0, 0, 0 };
}

// Noise shaper: a1=0, a2=-a0, b3=0 -> pad with 0 for uniform ROM
// H(s) = k0*s / (1 + k1*s + k2*s^2), normalized (cclock=1):
//   k0 = c2t*c3*c2b/c4    (dimensionless)
//   k1 = c2t*c2b          (cclock cancels from MAME's k1=c2t*(cclock*c2b) after normalization)
//   k2 = c1*c2t*c3/c4     (cclock cancels from MAME's k2=c1*c2t*c3/(cclock*c4))
// fpeak_norm = sqrt(1/k2_norm)/(2*pi) is clock-independent -> prewarping works correctly.
Coeffs build_noise_shaper(double c1, double c2t, double c2b,
                           double c3, double c4)
{
    double k0 = c2t * c3 * c2b / c4;
    double k1 = c2t * c2b;
    double k2 = c1  * c2t * c3 / c4;

    double fpeak_norm = sqrt(1.0 / k2) / (2*M_PI);
    double zn  = prewarp_zc(fpeak_norm);
    double zn2 = zn * zn;

    double m0 = zn  * k0;
    double m1 = zn  * k1;
    double m2 = zn2 * k2;

    double b0 = 1+m1+m2;
    return { 1.0, m0/b0, 0, -m0/b0, 0, (2-2*m2)/b0, (1-m1+m2)/b0, 0 };
}

// Injection filter (F2N): noise injection at F2 output (mixed before F3).
//
// MAME uses: build_injection_filter(c1b=29154, c2t=829+Q, c2b=38180, c3=2352+F, c4=34270)
// Same Q/F sweep as F2V → same 512-entry address space (12-bit addr, 4096 ROM entries).
//
// MAME's H(s) = (k0 + k2*s) / (k1 - k2*s) is neutralized because the sign of k1
// varies across the Q/F range, making the denominator sometimes zero or giving a
// right-half-plane pole.
//
// Universal stable formula: choose denominator sign based on k1 sign:
//   k1 >= 0: use (k1 + m) as b0  →  pole = -(k1-m)/(k1+m), |pole| < 1 ✓
//   k1 <  0: use (k1 - m) as b0  →  pole = -(k1+m)/(k1-m), |pole| < 1 ✓
// Proof: |b1/b0| = |(k1∓m)/(k1±m)| < 1 whenever k1*m don't change sign — holds by
// construction since both cases have |b0| = |k1|+m > |b1| = ||k1|-m|.
//
// c4 is present in the call but unused in the filter equations (Galibert's derivation).
Coeffs build_injection(double c1b, double c2t, double c2b,
                        double c3, double /* c4 */)
{
    static constexpr double T = 2.0 * SCLOCK_NORM; // = 4.0

    // Normalized (cclock=1) k values from MAME derivation
    double k0_n = c2t;
    double k1_n = c1b * c3 / c2t - c2t;
    double m_n  = T * c2b;

    // Stable denominator: pick sign so that b0 yields |pole| < 1 always
    double b0_raw = (k1_n >= 0.0) ? (k1_n + m_n) : (k1_n - m_n);
    double b1_raw = (k1_n >= 0.0) ? (k1_n - m_n) : (k1_n + m_n);
    double a_pos  = k0_n + m_n;
    double a_neg  = k0_n - m_n;

    if (fabs(b0_raw) < 1e-12) {
        fprintf(stderr, "build_injection: b0≈0 at c2t=%.0f c3=%.0f, using pass-through\n",
                c2t, c3);
        return { 1.0, 1.0, 0, 0, 0, 0, 0, 0 };
    }

    // ROM layout: N_X=2, N_Y=2 → iir_filter_slow reads a0 (idx1), a1 (idx2), b1 (idx5)
    return { 1.0, a_pos/b0_raw, a_neg/b0_raw, 0, 0, b1_raw/b0_raw, 0, 0 };
}

// ============================================================
// Pole analysis (stability check)
// ============================================================

// Evaluate monic polynomial z^n + p[0]*z^{n-1} + ... + p[n-1] via Horner
static std::complex<double> poly_eval(const double *p, int n, std::complex<double> z)
{
    std::complex<double> r(1.0, 0.0);
    for (int i = 0; i < n; i++)
        r = r * z + p[i];
    return r;
}

// Durand-Kerner root finding for monic polynomial z^n + p[0]*z^{n-1} + ... + p[n-1]
static void find_roots_dkr(const double *p, int n, std::complex<double> *roots)
{
    const std::complex<double> w(0.4, 0.9);
    for (int i = 0; i < n; i++)
        roots[i] = std::pow(w, i);

    for (int iter = 0; iter < 500; iter++) {
        double err = 0.0;
        for (int i = 0; i < n; i++) {
            std::complex<double> val = poly_eval(p, n, roots[i]);
            std::complex<double> den(1.0, 0.0);
            for (int j = 0; j < n; j++)
                if (j != i) den *= (roots[i] - roots[j]);
            std::complex<double> delta = val / den;
            roots[i] -= delta;
            err = std::max(err, std::abs(delta));
        }
        if (err < 1e-12) break;
    }
}

// Compute poles (roots of denominator) for a Coeffs. Returns pole count.
//
// build_standard always embeds a guaranteed z=-1 pole/zero pair that cancels
// in H(z). We detect this (b2 == b1+b3-1) and factor it out analytically so
// the stability check only sees the 2 meaningful bandpass poles.
static int get_poles(const Coeffs &c, std::complex<double> poles[3],
                     bool *nyquist_factored = nullptr)
{
    if (nyquist_factored) *nyquist_factored = false;

    int order;
    double p[3];
    if (std::fabs(c.b3) > 1e-9) {
        p[0] = c.b1; p[1] = c.b2; p[2] = c.b3; order = 3;
    } else if (std::fabs(c.b2) > 1e-9) {
        p[0] = c.b1; p[1] = c.b2; order = 2;
    } else {
        order = 1;
    }

    if (order == 3 && std::fabs(c.b2 - (c.b1 + c.b3 - 1.0)) < 1e-6) {
        // Factor out (z+1): z^3+b1*z^2+b2*z+b3 = (z+1)*(z^2+(b1-1)*z+b3)
        double quad[2] = { c.b1 - 1.0, c.b3 };
        find_roots_dkr(quad, 2, poles);
        if (nyquist_factored) *nyquist_factored = true;
        return 2;
    }

    if (order == 1) {
        poles[0] = -c.b1;
    } else {
        find_roots_dkr(p, order, poles);
    }
    return order;
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

    for (uint32_t qb = 0; qb < 16; qb++) {
        double c2t = 829 + bits_to_caps(qb, {1390, 2965, 5875, 11297});
        for (uint32_t fb = 0; fb < 32; fb++) {
            double c3 = 2352 + bits_to_caps(fb, {833, 1663, 3164, 6327, 12654});
            check(build_injection(29154, c2t, 38180, c3, 34270));
        }
    }

    fprintf(stderr, "Global coeff range: [%f, %f]\n", gmin, gmax);
    fprintf(stderr, "s(2.15) range:      [%f, %f]\n",
            (double)FP_MIN/FP_SCALE, (double)FP_MAX/FP_SCALE);
    fprintf(stderr, "Fits: %s\n\n",
            (gmin >= (double)FP_MIN/FP_SCALE && gmax <= (double)FP_MAX/FP_SCALE)
            ? "YES" : "NO - OVERFLOW!");
}

// ============================================================
// Stability check: pole radii for all filter settings
// ============================================================
void stability_check()
{
    const double NEAR_LO = 0.99;  // warn if pole radius >= this (still stable)
    const double NEAR_HI = 1.01;  // warn if pole radius <= this (barely unstable)

    struct Stats {
        const char *name;
        double max_r = 0.0, min_r = 1e9;
        int n_unstable = 0, n_near = 0;
        char worst[64] = "";
    };

    auto process = [&](Stats &s, const Coeffs &c, const char *label = nullptr) {
        std::complex<double> poles[3];
        int n = get_poles(c, poles);
        for (int i = 0; i < n; i++) {
            double r = std::abs(poles[i]);
            if (r > s.max_r) {
                s.max_r = r;
                if (label) snprintf(s.worst, sizeof(s.worst), "%s", label);
            }
            if (r < s.min_r) s.min_r = r;
            if (r > 1.0 + 1e-6)                             s.n_unstable++;
            else if (r >= NEAR_LO && r <= 1.0 + 1e-6)       s.n_near++;
        }
    };

    Stats f1{"F1"}, f2v{"F2V"}, f3{"F3"}, f4{"F4"}, fx{"FX"}, fn{"FN"}, f2n{"F2N"};
    char lbl[64];

    // F1 (16 settings)
    for (uint32_t b = 0; b < 16; b++) {
        snprintf(lbl, sizeof(lbl), "b=%u", b);
        process(f1, build_standard(11247, 11797, 949, 52067,
                2280 + bits_to_caps(b, {2546, 4973, 9861, 19724}), 166272), lbl);
    }

    // F2V (16×32 settings)
    for (uint32_t qb = 0; qb < 16; qb++) {
        double c2t = 829 + bits_to_caps(qb, {1390, 2965, 5875, 11297});
        for (uint32_t fb = 0; fb < 32; fb++) {
            snprintf(lbl, sizeof(lbl), "q=%u,f=%u", qb, fb);
            process(f2v, build_standard(24840, 29154, c2t, 38180,
                    2352 + bits_to_caps(fb, {833, 1663, 3164, 6327, 12654}), 34270), lbl);
        }
    }

    // F3 (16 settings)
    for (uint32_t b = 0; b < 16; b++) {
        snprintf(lbl, sizeof(lbl), "b=%u", b);
        process(f3, build_standard(0, 17594, 868, 18828,
                8480 + bits_to_caps(b, {2226, 4485, 9056, 18111}), 50019), lbl);
    }

    // F4, FX, FN (single settings)
    process(f4,  build_standard(0, 28810, 1165, 21457, 8558, 7289),  "fixed");
    process(fx,  build_lowpass(1122, 23131),                          "fixed");
    process(fn,  build_noise_shaper(15500, 14854, 8450, 9523, 14083), "fixed");

    // F2N (16×32 settings)
    for (uint32_t qb = 0; qb < 16; qb++) {
        double c2t = 829 + bits_to_caps(qb, {1390, 2965, 5875, 11297});
        for (uint32_t fb = 0; fb < 32; fb++) {
            double c3 = 2352 + bits_to_caps(fb, {833, 1663, 3164, 6327, 12654});
            snprintf(lbl, sizeof(lbl), "q=%u,f=%u", qb, fb);
            process(f2n, build_injection(29154, c2t, 38180, c3, 34270), lbl);
        }
    }

    fprintf(stderr, "=== Stability check (|z| of poles) ===\n");
    fprintf(stderr, "  Note: |z| < 1 = stable; |z| >= 0.99 = near unit circle; |z| >= 1 = UNSTABLE\n\n");

    auto report = [&](const Stats &s) {
        fprintf(stderr, "  %-6s: |z| in [%.6f, %.6f]  worst=%s",
                s.name, s.min_r, s.max_r, s.worst[0] ? s.worst : "?");
        if (s.n_unstable) fprintf(stderr, "  *** UNSTABLE: %d poles ***", s.n_unstable);
        if (s.n_near)     fprintf(stderr, "  [%d pole(s) near unit circle (>=%.2f)]", s.n_near, NEAR_LO);
        fprintf(stderr, "\n");
    };

    report(f1); report(f2v); report(f3); report(f4);
    report(fx);  report(fn);  report(f2n);
    fprintf(stderr, "\n");
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
    static constexpr int VOTRAX_FP_FRAC  = 15;
    fprintf(f, "static constexpr int VOTRAX_FP_FRAC = %d;\n", FP_FRAC);
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

    // F2N injection: 512 settings × 8 coeffs = 4096 entries (same addr as f2v_rom)
    {
        fprintf(f, "// F2N: 512 settings × 8 coeffs = 4096 entries\n");
        fprintf(f, "// addr = (filt_f2q << 8) | (filt_f2 << 3) | coeff_idx\n");
        fprintf(f, "// Only a0 (idx1), a1 (idx2), b1 (idx5) are non-zero (1st-order filter)\n");
        fprintf(f, "static const int32_t f2n_rom[4096] = {\n");
        for (uint32_t qb = 0; qb < 16; qb++) {
            double c2t = 829 + bits_to_caps(qb, {1390, 2965, 5875, 11297});
            for (uint32_t fb = 0; fb < 32; fb++) {
                double c3 = 2352 + bits_to_caps(fb, {833, 1663, 3164, 6327, 12654});
                int32_t entry[8];
                fill(entry, build_injection(29154, c2t, 38180, c3, 34270));
                int base = (qb*32+fb)*8;
                fprintf(f, "    ");
                for (int i = 0; i < 8; i++)
                    fprintf(f, "%7d%s", entry[i], (base+i < 4095) ? "," : " ");
                fprintf(f, "  // q=%u f=%u\n", qb, fb);
            }
        }
        fprintf(f, "};\n\n");
    }
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

    // ---- F2N (injection): 4096 entries, 12-bit addr (same as F2V) ----
    {
        int32_t data[4096];
        for (uint32_t qb = 0; qb < 16; qb++) {
            double c2t = 829 + bits_to_caps(qb, {1390, 2965, 5875, 11297});
            for (uint32_t fb = 0; fb < 32; fb++) {
                double c3 = 2352 + bits_to_caps(fb, {833, 1663, 3164, 6327, 12654});
                fill(&data[(qb*32+fb)*8],
                     build_injection(29154, c2t, 38180, c3, 34270));
            }
        }
        snprintf(path, sizeof(path), "%s/f2n_rom.vhd", outdir);
        FILE *f = fopen(path, "w");
        gen_vhdl_rom_entity(f, "f2n_rom", 12, 4096, data);
        fclose(f);
        fprintf(stderr, "Written: %s\n", path);
    }
}

// ============================================================
int main()
{
    sanity_check();
    stability_check();

    FILE *f = fopen("/tmp/votrax_rom_tables.h", "w");
    gen_c_header(f);
    fclose(f);
    fprintf(stderr, "C header: /tmp/votrax_rom_tables.h\n");

    gen_vhdl_rom_entities("/tmp");

    return 0;
}