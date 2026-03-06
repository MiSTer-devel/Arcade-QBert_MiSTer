// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/***************************************************************************

    votrax.h

    Votrax SC01A simulation

***************************************************************************/
#ifndef MAME_SOUND_VOTRAX_H
#define MAME_SOUND_VOTRAX_H

#pragma once

#include "votrax_rom_tables.h"

class votrax_sc01_device :  public device_t,
							public device_sound_interface
{
public:
	// construction/destruction
	votrax_sc01_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	auto ar_callback() { return m_ar_cb.bind(); }

	void write(uint8_t data);
	void inflection_w(uint8_t data);
	int request() { m_stream->update(); return m_ar_state; }

protected:
	// overridable type for subclass
	votrax_sc01_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);
	// device-level overrides
	virtual const tiny_rom_entry *device_rom_region() const override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_clock_changed() override;

	// device_sound_interface overrides
	virtual void sound_stream_update(sound_stream &stream) override;

	TIMER_CALLBACK_MEMBER(phone_tick);

private:
	// Possible timer parameters
	enum {
		T_COMMIT_PHONE,
		T_END_OF_PHONE
	};

	static const char *const s_phone_table[64];

	// Glottal wave in s(2.15) fixed-point
	// Original: 0, -4/7, 1, 6/7, 5/7, 4/7, 3/7, 2/7, 1/7
	static const int32_t s_glottal_wave[9];

	sound_stream *m_stream;                         // Output stream
	emu_timer *m_timer;                             // General timer
	required_memory_region m_rom;                   // Internal ROM
	u32 m_mainclock;                                // Current main clock
	double m_sclock;                                // Stream sample clock (main/18)
	double m_cclock;                                // Capacitor switching clock (main/36)
	u32 m_sample_count;                             // Sample counter

	// Inputs
	u8 m_inflection;                                // 2-bit inflection value
	u8 m_phone;                                     // 6-bit phone value

	// Outputs
	devcb_write_line m_ar_cb;
	bool m_ar_state;

	// "Unpacked" current rom values
	u8 m_rom_duration;
	u8 m_rom_vd, m_rom_cld;
	u8 m_rom_fa, m_rom_fc, m_rom_va;
	u8 m_rom_f1, m_rom_f2, m_rom_f2q, m_rom_f3;
	bool m_rom_closure;
	bool m_rom_pause;

	// Current interpolated values (8 bits each)
	u8 m_cur_fa, m_cur_fc, m_cur_va;
	u8 m_cur_f1, m_cur_f2, m_cur_f2q, m_cur_f3;

	// Current committed values
	u8 m_filt_fa, m_filt_fc, m_filt_va;
	u8 m_filt_f1, m_filt_f2, m_filt_f2q, m_filt_f3;

	// Internal counters
	u16 m_phonetick;
	u8  m_ticks;
	u8  m_pitch;
	u8  m_closure;
	u8  m_update_counter;

	// Internal state
	bool m_cur_closure;
	u16 m_noise;
	bool m_cur_noise;

	// ROM base addresses for variable filters (f4/fx/fn constant = base 0)
	// addr = (filter_param << 3) | coeff_idx
	// coeff_idx: 0=b0(=1.0) 1=a0 2=a1 3=a2 4=a3 5=b1 6=b2 7=b3
	uint32_t m_f1_addr;
	uint32_t m_f2v_addr;
	uint32_t m_f3_addr;

	// Signal path history arrays - s(2.15) fixed-point
	// All signals fit in s(2.15): worst case ~3.1, range [-4.0, +4.0]
	int32_t m_voice_1[4];
	int32_t m_voice_2[4];
	int32_t m_voice_3[4];

	int32_t m_noise_1[3];
	int32_t m_noise_2[3];
	int32_t m_noise_3[2];
	int32_t m_noise_4[2];

	int32_t m_vn_1[4];
	int32_t m_vn_2[4];
	int32_t m_vn_3[4];
	int32_t m_vn_4[4];
	int32_t m_vn_5[2];
	int32_t m_vn_6[2];

	// Multiply two s(2.15) values → s(2.15)
	// result = (a * b) >> 15
	static inline int32_t fp_mul(int32_t a, int32_t b) {
		return (int32_t)(((int64_t)a * b) >> VOTRAX_FP_FRAC);
	}

	// Scale by 4-bit volume (0..15): val * vol / 15
	// Approximation: 15 ≈ 32768/2185, so val * vol * 2185 >> 15
	// Error < 0.01%
	static inline int32_t fp_scale15(int32_t val, u8 vol) {
		return (int32_t)(((int64_t)val * vol * 2185) >> VOTRAX_FP_FRAC);
	}

	// Scale by 3-bit closure (0..7): val * clos / 7
	// Approximation: 7 ≈ 32768/4681, so val * clos * 4681 >> 15
	// Error < 0.01%
	static inline int32_t fp_scale7(int32_t val, u8 clos) {
		return (int32_t)(((int64_t)val * clos * 4681) >> VOTRAX_FP_FRAC);
	}

	// Shift history by one, insert new value at front
	template<u32 N> static void shift_hist(int32_t val, int32_t (&hist)[N]) {
		for(u32 i=N-1; i>0; i--)
			hist[i] = hist[i-1];
		hist[0] = val;
	}

	// Apply IIR filter from ROM using integer MAC.
	// y[n] = sum(x[i] * a[i]) - sum(y[i] * b[i+1])
	// All values s(2.15), MAC uses int64_t, result >> 15.
	// b0 = 1.0 (normalized), not stored, no division needed.
	// ROM layout: base+0=b0, base+1=a0..base+4=a3, base+5=b1..base+7=b3
	template<u32 Nx, u32 Ny>
	static int32_t apply_filter(const int32_t (&x)[Nx], const int32_t (&y)[Ny],
	                            const int32_t *rom, uint32_t base)
	{
		int64_t acc = 0;
		for(u32 i = 0; i < Nx; i++)
			acc += (int64_t)x[i] * rom[base + 1 + i];
		for(u32 i = 0; i < Ny-1; i++)
			acc -= (int64_t)y[i] * rom[base + 5 + i];
		return (int32_t)(acc >> VOTRAX_FP_FRAC);
	}

	void build_injection_filter(double *a, double *b,
								double c1b,
								double c2t,
								double c2b,
								double c3,
								double c4);

	static void interpolate(u8 &reg, u8 target);
	void chip_update();
	void filters_commit(bool force);
	void phone_commit();
	sound_stream::sample_t analog_calc();
};

class votrax_sc01a_device : public votrax_sc01_device
{
public:
	votrax_sc01a_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
protected:
	virtual const tiny_rom_entry *device_rom_region() const override ATTR_COLD;
};

DECLARE_DEVICE_TYPE(VOTRAX_SC01, votrax_sc01_device)
DECLARE_DEVICE_TYPE(VOTRAX_SC01A, votrax_sc01a_device)

#endif // MAME_SOUND_VOTRAX_H