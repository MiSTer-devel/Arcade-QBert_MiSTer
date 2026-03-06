// votrax_dump.h
// Include this in votrax.cpp to enable input/output logging
// for GHDL testbench generation.
//
// Usage: #define VOTRAX_DUMP before including, or add to build flags
// Output: votrax_input.txt  - phone/inflection/clock events
//         votrax_output.txt - expected audio samples
//
// Format: one event per line, key=value pairs, easy to parse
//   ts=<milliseconds> event=<type> [key=value ...]
//
// Input events:
//   ts=12.450 event=CLOCK freq=720000
//   ts=12.450 event=PHONE phone=2A inflection=01
//   ts=12.450 event=INFLECTION inflection=01
//   ts=12.450 event=RESET
//
// Output events:
//   ts=0.045 event=SAMPLE sample=142

#pragma once
#define VOTRAX_DUMP
#include <stdio.h>
#include <stdlib.h>
#ifdef VOTRAX_DUMP

#include <cstdio>

static FILE *s_votrax_in  = nullptr;
static FILE *s_votrax_out = nullptr;

static void votrax_dump_init()
{
    if(!s_votrax_in) {
        s_votrax_in  = fopen("votrax_input.txt",  "w");
        s_votrax_out = fopen("votrax_output.txt", "w");
        fprintf(s_votrax_in,  "# Votrax SC01A input event log\n");
        fprintf(s_votrax_in,  "# ts=milliseconds event=type [key=value ...]\n");
        fprintf(s_votrax_out, "# Votrax SC01A expected audio output\n");
        fprintf(s_votrax_out, "# ts=milliseconds event=SAMPLE sample=int32(s2.15)\n");
    }
}

// Call at start of each relevant function to get current time in ms
// Uses machine().time() which is available in device context
#define VOTRAX_DUMP_INIT()  votrax_dump_init()
#define VOTRAX_DUMP_TS()    (machine().time().as_double() * 1000.0)

#define VOTRAX_DUMP_CLOCK(freq) \
    do { VOTRAX_DUMP_INIT(); \
         fprintf(s_votrax_in, "ts=%.6f event=CLOCK freq=%u\n", \
                 VOTRAX_DUMP_TS(), (unsigned)(freq)); \
         fflush(s_votrax_in); } while(0)

#define VOTRAX_DUMP_PHONE(phone, inflection) \
    do { VOTRAX_DUMP_INIT(); \
         fprintf(s_votrax_in, "ts=%.6f event=PHONE phone=%02X inflection=%u\n", \
                 VOTRAX_DUMP_TS(), (unsigned)(phone), (unsigned)(inflection)); \
         fflush(s_votrax_in); } while(0)

#define VOTRAX_DUMP_INFLECTION(inflection) \
    do { VOTRAX_DUMP_INIT(); \
         fprintf(s_votrax_in, "ts=%.6f event=INFLECTION inflection=%u\n", \
                 VOTRAX_DUMP_TS(), (unsigned)(inflection)); \
         fflush(s_votrax_in); } while(0)

#define VOTRAX_DUMP_RESET() \
    do { VOTRAX_DUMP_INIT(); \
         fprintf(s_votrax_in, "ts=%.6f event=RESET\n", \
                 VOTRAX_DUMP_TS()); \
         fflush(s_votrax_in); } while(0)

#define VOTRAX_DUMP_SAMPLE(sample) \
    do { VOTRAX_DUMP_INIT(); \
         fprintf(s_votrax_out, "ts=%.6f event=SAMPLE sample=%d\n", \
                 VOTRAX_DUMP_TS(), (int)(sample)); \
         fflush(s_votrax_out); } while(0)

#else

// No-ops when dump disabled
#define VOTRAX_DUMP_INIT()
#define VOTRAX_DUMP_CLOCK(freq)
#define VOTRAX_DUMP_PHONE(phone, inflection)
#define VOTRAX_DUMP_INFLECTION(inflection)
#define VOTRAX_DUMP_RESET()
#define VOTRAX_DUMP_SAMPLE(sample)

#endif // VOTRAX_DUMP