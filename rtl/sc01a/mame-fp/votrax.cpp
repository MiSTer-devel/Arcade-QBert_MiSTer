// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/***************************************************************************

    votrax.c

    Votrax SC01A simulation

fixed point version
modified for sc01a.vhd

***************************************************************************/

#include "emu.h"
#include "votrax.h"
#include "votrax_dump.h"

#define LOG_PHONE  (1U << 1)
#define LOG_COMMIT (1U << 2)
#define LOG_INT    (1U << 3)
#define LOG_TICK   (1U << 4)
#define LOG_FILTER (1U << 5)

//#define VERBOSE (LOG_GENERAL | LOG_PHONE)
#include "logmacro.h"


DEFINE_DEVICE_TYPE(VOTRAX_SC01, votrax_sc01_device, "votrsc01", "Votrax SC-01")
DEFINE_DEVICE_TYPE(VOTRAX_SC01A, votrax_sc01a_device, "votrsc01a", "Votrax SC-01-A")

ROM_START( votrax_sc01 )
	ROM_REGION64_LE( 0x200, "internal", 0 )
	ROM_LOAD( "sc01.bin", 0x000, 0x200, CRC(528d1c57) SHA1(268b5884dce04e49e2376df3e2dc82e852b708c1) )
ROM_END

ROM_START( votrax_sc01a )
	ROM_REGION64_LE( 0x200, "internal", 0 )
	ROM_LOAD( "sc01a.bin", 0x000, 0x200, CRC(fc416227) SHA1(1d6da90b1807a01b5e186ef08476119a862b5e6d) )
ROM_END

const char *const votrax_sc01_device::s_phone_table[64] =
{
	"EH3",  "EH2",  "EH1",  "PA0",  "DT",   "A1",   "A2",   "ZH",
	"AH2",  "I3",   "I2",   "I1",   "M",    "N",    "B",    "V",
	"CH",   "SH",   "Z",    "AW1",  "NG",   "AH1",  "OO1",  "OO",
	"L",    "K",    "J",    "H",    "G",    "F",    "D",    "S",
	"A",    "AY",   "Y1",   "UH3",  "AH",   "P",    "O",    "I",
	"U",    "Y",    "T",    "R",    "E",    "W",    "AE",   "AE1",
	"AW2",  "UH2",  "UH1",  "UH",   "O2",   "O1",   "IU",   "U1",
	"THV",  "TH",   "ER",   "EH",   "E1",   "AW",   "PA1",  "STOP"
};

// Glottal wave in s(2.15): original values * 32768
// 0, -4/7, 7/7, 6/7, 5/7, 4/7, 3/7, 2/7, 1/7
const int32_t votrax_sc01_device::s_glottal_wave[9] = {
	0,                              //  0.000
	(int32_t)(-4*32768/7),          // -0.571  = -18725
	(int32_t)( 7*32768/7),          // +1.000  = +32768  (clamped to 32767)
	(int32_t)( 6*32768/7),          // +0.857  = +28101
	(int32_t)( 5*32768/7),          // +0.714  = +23405
	(int32_t)( 4*32768/7),          // +0.571  = +18725
	(int32_t)( 3*32768/7),          // +0.429  = +14050
	(int32_t)( 2*32768/7),          // +0.286  =  +9362
	(int32_t)( 1*32768/7),          // +0.143  =  +4681
};

votrax_sc01_device::votrax_sc01_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: votrax_sc01_device(mconfig, VOTRAX_SC01, tag, owner, clock)
{
}

votrax_sc01_device::votrax_sc01_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, type, tag, owner, clock),
	  device_sound_interface(mconfig, *this),
	  m_stream(nullptr),
	  m_rom(*this, "internal"),
	  m_ar_cb(*this)
{
}

votrax_sc01a_device::votrax_sc01a_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: votrax_sc01_device(mconfig, VOTRAX_SC01A, tag, owner, clock)
{
}

void votrax_sc01_device::write(uint8_t data)
{
	m_stream->update();

	u8 prev = m_phone;
	m_phone = data & 0x3f;

	if(m_phone != prev || m_phone != 0x3f)
		LOGMASKED(LOG_PHONE, "phone %02x.%d %s\n", m_phone, m_inflection, s_phone_table[m_phone]);

	VOTRAX_DUMP_PHONE(m_phone, m_inflection);

    printf("%d\n", (int) m_phone);
	m_ar_state = CLEAR_LINE;
	m_ar_cb(m_ar_state);

	if(m_timer->expire().is_never() || m_timer->param() != T_COMMIT_PHONE)
		m_timer->adjust(attotime::from_ticks(72, m_mainclock), T_COMMIT_PHONE);
}

void votrax_sc01_device::inflection_w(uint8_t data)
{
	data &= 3;
	if(m_inflection == data)
		return;
	VOTRAX_DUMP_INFLECTION(data);
	m_stream->update();
	m_inflection = data;
}

void votrax_sc01_device::sound_stream_update(sound_stream &stream)
{
    //printf("SC01 sample rate = %d\n", m_stream->sample_rate());

	for(int i=0; i<stream.samples(); i++) {
		m_sample_count++;
		if(m_sample_count & 1)
			chip_update();
		stream.put(0, i, analog_calc());
	}
}

const tiny_rom_entry *votrax_sc01_device::device_rom_region() const
{
	return ROM_NAME( votrax_sc01 );
}

const tiny_rom_entry *votrax_sc01a_device::device_rom_region() const
{
	return ROM_NAME( votrax_sc01a );
}

void votrax_sc01_device::device_start()
{
	m_mainclock = clock();
	m_sclock = m_mainclock / 18.0;
	m_cclock = m_mainclock / 36.0;
	m_stream = stream_alloc(0, 1, m_sclock);
	m_timer = timer_alloc(FUNC(votrax_sc01_device::phone_tick), this);

	m_ar_state = ASSERT_LINE;

	save_item(NAME(m_inflection));
	save_item(NAME(m_phone));
	save_item(NAME(m_ar_state));
	save_item(NAME(m_rom_duration));
	save_item(NAME(m_rom_vd));
	save_item(NAME(m_rom_cld));
	save_item(NAME(m_rom_fa));
	save_item(NAME(m_rom_fc));
	save_item(NAME(m_rom_va));
	save_item(NAME(m_rom_f1));
	save_item(NAME(m_rom_f2));
	save_item(NAME(m_rom_f2q));
	save_item(NAME(m_rom_f3));
	save_item(NAME(m_rom_closure));
	save_item(NAME(m_rom_pause));
	save_item(NAME(m_cur_fa));
	save_item(NAME(m_cur_fc));
	save_item(NAME(m_cur_va));
	save_item(NAME(m_cur_f1));
	save_item(NAME(m_cur_f2));
	save_item(NAME(m_cur_f2q));
	save_item(NAME(m_cur_f3));
	save_item(NAME(m_filt_fa));
	save_item(NAME(m_filt_fc));
	save_item(NAME(m_filt_va));
	save_item(NAME(m_filt_f1));
	save_item(NAME(m_filt_f2));
	save_item(NAME(m_filt_f2q));
	save_item(NAME(m_filt_f3));
	save_item(NAME(m_phonetick));
	save_item(NAME(m_ticks));
	save_item(NAME(m_pitch));
	save_item(NAME(m_closure));
	save_item(NAME(m_update_counter));
	save_item(NAME(m_cur_closure));
	save_item(NAME(m_noise));
	save_item(NAME(m_cur_noise));
	save_item(NAME(m_voice_1));
	save_item(NAME(m_voice_2));
	save_item(NAME(m_voice_3));
	save_item(NAME(m_noise_1));
	save_item(NAME(m_noise_2));
	save_item(NAME(m_noise_3));
	save_item(NAME(m_noise_4));
	save_item(NAME(m_vn_1));
	save_item(NAME(m_vn_2));
	save_item(NAME(m_vn_3));
	save_item(NAME(m_vn_4));
	save_item(NAME(m_vn_5));
	save_item(NAME(m_vn_6));
	save_item(NAME(m_f1_addr));
	save_item(NAME(m_f2v_addr));
	save_item(NAME(m_f3_addr));
}

void votrax_sc01_device::device_reset()
{
	m_phone = 0x3f;
	m_inflection = 0;
	m_ar_state = ASSERT_LINE;
	m_ar_cb(m_ar_state);
	VOTRAX_DUMP_RESET();

	m_sample_count = 0;

	phone_commit();

	m_cur_fa = m_cur_fc = m_cur_va = 0;
	m_cur_f1 = m_cur_f2 = m_cur_f2q = m_cur_f3 = 0;

	filters_commit(true);

	m_pitch = 0;
	m_closure = 0;
	m_update_counter = 0;
	m_cur_closure = true;
	m_noise = 0;
	m_cur_noise = false;

	memset(m_voice_1, 0, sizeof(m_voice_1));
	memset(m_voice_2, 0, sizeof(m_voice_2));
	memset(m_voice_3, 0, sizeof(m_voice_3));
	memset(m_noise_1, 0, sizeof(m_noise_1));
	memset(m_noise_2, 0, sizeof(m_noise_2));
	memset(m_noise_3, 0, sizeof(m_noise_3));
	memset(m_noise_4, 0, sizeof(m_noise_4));
	memset(m_vn_1, 0, sizeof(m_vn_1));
	memset(m_vn_2, 0, sizeof(m_vn_2));
	memset(m_vn_3, 0, sizeof(m_vn_3));
	memset(m_vn_4, 0, sizeof(m_vn_4));
	memset(m_vn_5, 0, sizeof(m_vn_5));
	memset(m_vn_6, 0, sizeof(m_vn_6));
}

#include <execinfo.h>

void dump_stack()
{
    void* array[32];
    int size = backtrace(array, 32);
    backtrace_symbols_fd(array, size, 2); // 2 = stderr
}

void votrax_sc01_device::device_clock_changed()
{
    dump_stack();
	u32 newfreq = clock();
	if(newfreq != m_mainclock) {
		m_stream->update();
		VOTRAX_DUMP_CLOCK(newfreq);

		if(!m_timer->expire().is_never()) {
			u64 remaining = m_timer->remaining().as_ticks(m_mainclock);
			m_timer->adjust(attotime::from_ticks(remaining, newfreq));
		}
		m_mainclock = newfreq;
		m_sclock = m_mainclock / 18.0;
		m_cclock = m_mainclock / 36.0;
		m_stream->set_sample_rate(m_sclock);
		// Filter addresses are pure cap ratios, clock-independent.
	}
}

TIMER_CALLBACK_MEMBER(votrax_sc01_device::phone_tick)
{
	m_stream->update();

	switch(param) {
	case T_COMMIT_PHONE:
		phone_commit();
		m_timer->adjust(attotime::from_ticks(16*(m_rom_duration*4+1)*4*9+2, m_mainclock), T_END_OF_PHONE);
		break;
	case T_END_OF_PHONE:
		m_ar_state = ASSERT_LINE;
		break;
	}

	m_ar_cb(m_ar_state);
}

void votrax_sc01_device::phone_commit()
{
	m_phonetick = 0;
	m_ticks = 0;

	for(int i=0; i<64; i++) {
		u64 val = reinterpret_cast<const u64 *>(m_rom->base())[i];
		if(m_phone == ((val >> 56) & 0x3f)) {
			m_rom_f1  = bitswap(val,  0,  7, 14, 21);
			m_rom_va  = bitswap(val,  1,  8, 15, 22);
			m_rom_f2  = bitswap(val,  2,  9, 16, 23);
			m_rom_fc  = bitswap(val,  3, 10, 17, 24);
			m_rom_f2q = bitswap(val,  4, 11, 18, 25);
			m_rom_f3  = bitswap(val,  5, 12, 19, 26);
			m_rom_fa  = bitswap(val,  6, 13, 20, 27);
			m_rom_cld = bitswap(val, 34, 32, 30, 28);
			m_rom_vd  = bitswap(val, 35, 33, 31, 29);
			m_rom_closure  = bitswap(val, 36);
			m_rom_duration = bitswap(~val, 37, 38, 39, 40, 41, 42, 43);
			m_rom_pause = (m_phone == 0x03) || (m_phone == 0x3e);

			LOGMASKED(LOG_COMMIT, "commit fa=%x va=%x fc=%x f1=%x f2=%x f2q=%x f3=%x dur=%02x cld=%x vd=%d cl=%d pause=%d\n", m_rom_fa, m_rom_va, m_rom_fc, m_rom_f1, m_rom_f2, m_rom_f2q, m_rom_f3, m_rom_duration, m_rom_cld, m_rom_vd, m_rom_closure, m_rom_pause);

			if(m_rom_cld == 0)
				m_cur_closure = m_rom_closure;

			return;
		}
	}
}

void votrax_sc01_device::interpolate(u8 &reg, u8 target)
{
	reg = reg - (reg >> 3) + (target << 1);
}

void votrax_sc01_device::chip_update()
{
	if(m_ticks != 0x10) {
		m_phonetick++;
		if(m_phonetick == ((m_rom_duration << 2) | 1)) {
			m_phonetick = 0;
			m_ticks++;
			if(m_ticks == m_rom_cld)
				m_cur_closure = m_rom_closure;
		}
	}

	m_update_counter++;
	if(m_update_counter == 0x30)
		m_update_counter = 0;

	bool tick_625 = !(m_update_counter & 0xf);
	bool tick_208 = m_update_counter == 0x28;

	if(tick_208 && (!m_rom_pause || !(m_filt_fa || m_filt_va))) {
		interpolate(m_cur_fc,  m_rom_fc);
		interpolate(m_cur_f1,  m_rom_f1);
		interpolate(m_cur_f2,  m_rom_f2);
		interpolate(m_cur_f2q, m_rom_f2q);
		interpolate(m_cur_f3,  m_rom_f3);
		LOGMASKED(LOG_INT, "int fa=%x va=%x fc=%x f1=%x f2=%02x f2q=%02x f3=%x\n", m_cur_fa >> 4, m_cur_va >> 4, m_cur_fc >> 4, m_cur_f1 >> 4, m_cur_f2 >> 3, m_cur_f2q >> 4, m_cur_f3 >> 4);
	}

	if(tick_625) {
		if(m_ticks >= m_rom_vd)
			interpolate(m_cur_fa, m_rom_fa);
		if(m_ticks >= m_rom_cld) {
			interpolate(m_cur_va, m_rom_va);
			LOGMASKED(LOG_INT, "int fa=%x va=%x fc=%x f1=%x f2=%02x f2q=%02x f3=%x\n", m_cur_fa >> 4, m_cur_va >> 4, m_cur_fc >> 4, m_cur_f1 >> 4, m_cur_f2 >> 3, m_cur_f2q >> 4, m_cur_f3 >> 4);
		}
	}

	if(!m_cur_closure && (m_filt_fa || m_filt_va))
		m_closure = 0;
	else if(m_closure != 7 << 2)
		m_closure++;

	m_pitch = (m_pitch + 1) & 0xff;
	if(m_pitch == (0xe0 ^ (m_inflection << 5) ^ (m_filt_f1 << 1)) + 2)
		m_pitch = 0;

	if((m_pitch & 0xf9) == 0x08)
		filters_commit(false);

	bool inp = (1||m_filt_fa) && m_cur_noise && (m_noise != 0x7fff);
	m_noise = ((m_noise << 1) & 0x7ffe) | inp;
	m_cur_noise = !(((m_noise >> 14) ^ (m_noise >> 13)) & 1);

/*
        printf("m_ticks=%d, m_phonetick=%d, m_update_counter=%d, noise=%d cur_noise=%d, pitch=%d, closure=%d, rom_duration=%d, cur_fc=%d, cur_f1=%d, cur_f2=%d, cur_f2q=%d, cur_f3=%d\n",
        (int)m_ticks, (int)m_phonetick, (int)m_update_counter, (int)m_noise,
        (int)m_cur_noise, (int)m_pitch, (int)m_closure, (int)m_rom_duration, (int)m_cur_fc, (int)m_cur_f1,
        (int)m_cur_f2, (int)m_cur_f2q, (int)m_cur_f3);
        printf("m_filt_f1=%d, m_filt_fa=%d m_filt_fc=%d, m_filt_va=%d, m_f1_addr=%d, m_filt_f2=%d, m_filt_f2q=%d, m_f2v_addr=%d, m_filt_f3=%d, m_f3_addr=%d\n",
        (int)m_filt_f1, (int)m_filt_fa, (int)m_filt_fc, (int)m_filt_va, (int)m_f1_addr, (int)m_filt_f2, (int)m_filt_f2q, (int)m_f2v_addr, (int)m_filt_f3, (int)m_f3_addr);
*/
	LOGMASKED(LOG_TICK, "%s tick %02x.%03x 625=%d 208=%d pitch=%02x.%x ns=%04x ni=%d noise=%d cl=%x.%x clf=%d/%d\n", machine().time().to_string(), m_ticks, m_phonetick, tick_625, tick_208, m_pitch >> 3, m_pitch & 7, m_noise, inp, m_cur_noise, m_closure >> 2, m_closure & 3, m_rom_closure, m_cur_closure);
}

void votrax_sc01_device::filters_commit(bool force)
{
	m_filt_fa = m_cur_fa >> 4;
	m_filt_fc = m_cur_fc >> 4;
	m_filt_va = m_cur_va >> 4;

	if(force || m_filt_f1 != m_cur_f1 >> 4) {
		m_filt_f1 = m_cur_f1 >> 4;
		m_f1_addr = m_filt_f1 << 3;
	}

	if(force || m_filt_f2 != m_cur_f2 >> 3 || m_filt_f2q != m_cur_f2q >> 4) {
		m_filt_f2  = m_cur_f2  >> 3;
		m_filt_f2q = m_cur_f2q >> 4;
		m_f2v_addr = ((m_filt_f2q << 5) | m_filt_f2) << 3;
	}

	if(force || m_filt_f3 != m_cur_f3 >> 4) {
		m_filt_f3 = m_cur_f3 >> 4;
		m_f3_addr = m_filt_f3 << 3;
	}

	// F4, FX, FN: constant ROMs, base 0, no commit needed.

	if(m_filt_fa | m_filt_va | m_filt_fc | m_filt_f1 | m_filt_f2 | m_filt_f2q | m_filt_f3)
		LOGMASKED(LOG_FILTER, "filter fa=%x va=%x fc=%x f1=%x f2=%02x f2q=%x f3=%x\n", m_filt_fa, m_filt_va, m_filt_fc, m_filt_f1, m_filt_f2, m_filt_f2q, m_filt_f3);
}

inline void fwrite_scaled(int32_t *v, size_t size, int num, FILE* f) {
    int32_t tmp = (*v) * 16384;
    fwrite(&tmp, size, num, f);
}

sound_stream::sample_t votrax_sc01_device::analog_calc()
{
	// All signals in s(2.15) fixed-point (scale = 32768).
	// Worst case signal level ~3.1, fits in s(2.15) range [-4.0, +4.0].
	// Multiplications: int32 * int32 → int64 >> 15 → int32.

	// Voice-only path.
	// 1. Pick up the pitch wave
	int32_t v = m_pitch >= (9 << 3) ? 0 : s_glottal_wave[m_pitch >> 3];

	// 2. Multiply by the voice volume (0..15), divide by 15
	v = fp_scale15(v, m_filt_va);
	shift_hist(v, m_voice_1);

	// 3. Apply the f1 filter
	v = apply_filter(m_voice_1, m_voice_2, f1_rom, m_f1_addr);
	shift_hist(v, m_voice_2);

	// 4. Apply the f2 filter, voice half
	v = apply_filter(m_voice_2, m_voice_3, f2v_rom, m_f2v_addr);
	shift_hist(v, m_voice_3);

	// Noise-only path.
	// 5. Pick up the noise: ±0.5 in s(2.15) = ±16384
	int32_t n = (m_pitch & 0x40 ? m_cur_noise : false) ? 16384 : -16384;
	n = fp_scale15(n, m_filt_fa);
	shift_hist(n, m_noise_1);

	// 6. Apply the noise shaper (fn_rom, constant, base = 0)
	n = apply_filter(m_noise_1, m_noise_2, fn_rom, 0);
	shift_hist(n, m_noise_2);

	// 7. Scale with f2 noise cutoff (0..15), divide by 15
	int32_t n2 = fp_scale15(n, m_filt_fc);
	shift_hist(n2, m_noise_3);

	// 8. F2 noise injection: neutralized → output = 0
	shift_hist(0, m_noise_4);

	// Mixed path.
	// 9. Add the f2 voice and f2 noise outputs
	int32_t vn = v; // + 0 (f2n neutralized)
	shift_hist(vn, m_vn_1);

	// 10. Apply the f3 filter
	vn = apply_filter(m_vn_1, m_vn_2, f3_rom, m_f3_addr);
	shift_hist(vn, m_vn_2);

	// 11. Second noise insertion: n * (5 + (15^filt_fc)) / 20
	// (5 + (15^filt_fc)) is in range [5, 20], divide by 20
	// Approximation: /20 = *1638 >> 15  (32768/20 = 1638.4)
	int32_t noise_scale = 5 + (15 ^ (int)m_filt_fc); // 5..20
	vn += (int32_t)(((int64_t)n * noise_scale * 1638) >> VOTRAX_FP_FRAC);
	shift_hist(vn, m_vn_3);

	// 12. Apply the f4 filter (constant, base = 0)
	vn = apply_filter(m_vn_3, m_vn_4, f4_rom, 0);
	shift_hist(vn, m_vn_4);

	// 13. Apply the glottal closure amplitude (0..7), divide by 7
	vn = fp_scale7(vn, 7 ^ (m_closure >> 2));
	shift_hist(vn, m_vn_5);

	// 14. Apply the final lowpass filter (fx_rom, constant, base = 0)
	vn = apply_filter(m_vn_5, m_vn_6, fx_rom, 0);
	shift_hist(vn, m_vn_6);

	// Convert s(2.15) back to float sample, apply final 0.35 gain
	// 32768 * 0.35 * 8 ≈ 91752 (empirical /8 factor for level matching)
    static int32_t last = 0;

    if (vn != last) {
	    VOTRAX_DUMP_SAMPLE(vn);
    }
    last = vn;
	return (sound_stream::sample_t)vn / 91752.0f;
}

void votrax_sc01_device::build_injection_filter(double *a, double *b,
												double c1b, double c2t,
												double c2b, double c3, double c4)
{
	// Numerically unstable - neutralized
	a[0] = 0; a[1] = 0;
	b[0] = 1; b[1] = 0;
}