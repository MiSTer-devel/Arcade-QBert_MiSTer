#!/usr/bin/env python3
# raw_to_wav.py
# Converts audio_out.raw (text file, one int16 per line) to WAV
#
# Usage: python3 raw_to_wav.py audio_out.raw audio_out.wav

import sys
import numpy as np
import scipy.io.wavfile as wav

SAMPLE_RATE = 48000# 52778  # 950000 / 18

if len(sys.argv) < 3:
    print(f"Usage: {sys.argv[0]} input.raw output.wav", file=sys.stderr)
    sys.exit(1)

samples = np.loadtxt(sys.argv[1], dtype=np.int16)
wav.write(sys.argv[2], SAMPLE_RATE, samples)
print(f"Written {len(samples)} samples ({len(samples)/SAMPLE_RATE:.2f}s) to {sys.argv[2]}")
