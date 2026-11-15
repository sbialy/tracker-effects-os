# Notice of the Sample Editor effects (.ptfx)

The Sample Editor effects Amplifier, Overdrive, Delay, Bitcrusher, Chorus, Flanger, Equalizer, Limiter and Compressor (`se_*.ptfx`, card folder `Effects/`) are ports
of `core/effector/mtEffect*.cpp` of the Polyend Tracker Community Edition firmware, MIT, Copyright (c) 2020-2026 Polyend Sp. z o.o. and Polyend
Tracker Community Edition Contributors, as carried into tracker-ff (`licenses/LICENSE-tracker-ff-MIT.txt`). They are the offline effects of the
Sample Editor that tracker-ff 1.4.0 moved out of its image to `Effects/*.ptfx` on the card. The header of every source file names its original and
what changed. The Equalizer filter is eqfil of Soundpipe (https://github.com/eljeff/Soundpipe/blob/master/modules/eqfil.c, MIT), based on csound eqfil.
The Flanger sine is `arm_sin_q15` of CMSIS-DSP (Arm Limited, Apache License 2.0, `licenses/LICENSE-OpenTracker-Apache-2.0.txt`), rewritten with its table so that
the plugin gives the same value for every 16 bit argument as the library of the firmware. The Bitcrusher, Flanger and Equalizer are linked with
the soft double routines of libgcc (GCC Runtime Library Exception).
