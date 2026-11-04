// Stock OS 1.2.1 drumline voices run inside a synth machine's own voices
// (opxy/stock_drumline.hpp). Stock registers drumline as engine 4, a sample-flag
// engine whose voices its alternate renderer drives with a per-voice patch block
// built from each key's saved settings. Here the machine supplies that patch, the
// synth's normal render path supplies pitch, and stock's own drumline code
// renders. Linked only into collections with a machine declaring
// resources.stockVoice "drumline"; docs/abi-1.2.1.md, "Stock voices".
#include "opxy/stock_drumline.hpp"
#include "opxy/q31.hpp"
#include "stock.hpp"

using opxy::i32;
using opxy::u32;
using opxy::drumline::Patch;
using opxy::drumline::Voice;
using opxy::platform::stock;

namespace {
// Stock's drumline voice: the constructor its factory (0x8d513d1c) calls on
// each static voice, and the method table it installs.
const u32 kConstruct = 0x8d513a58;
const u32 kVtable = 0x8404ce3c;
// Method-table entries: +16 note start, +12 the alternate renderer's block call
// (stock's +8 asserts). +28, note release, is never called: a synth's notes are
// shaped by its amp envelope.
const u32 kStart = 0x8d513bea, kRender = 0x11a06d28;
// Voice words the alternate renderer's note start (+20) sets before start: the
// key, and a Q31 offset its length control adds (0.5 adds nothing).
const u32 kKeyWord = 0x34 / 4, kLengthOffsetWord = 0x60 / 4;
// Note start (+16) draws a per-hit random value for each of the four controls
// at voice words 0x50..0x5c, which render scales by random and adds; the first
// is the pitch control's.
const u32 kPitchRandomWord = 0x50 / 4;
static_assert(sizeof(Voice) == 560, "Stock 1.2.1 drumline voice object");

// The patch block word 0: stock's 66 models (family * 6 + variant) as the
// centre of each 1/66 of the Q31 range; render reads (word >> 8) * 66 >> 23.
const u32 kModelStep = 0x01f07c1f, kModelCentre = 0x00f83e0f;
const i32 kCentre = 0x40000000;          // pan centre; track offsets 0.5 add nothing
const i32 kUnityGain = 0x08000000;       // stock's 0 dB per-key gain word
const u32 kKey = 60;                      // the key stock records for the voice
const u32 kTrackWords = 16;               // render reads track words 0..11
// The synth's amp envelope words, and drumline's track words they drive.
const u32 kAmpAttack = 8, kAmpDecay = 9, kDrumlineAttack = 9, kDrumlineLength = 11;

// Drumline reads the per-sample pitch buffer as stock's alternate renderer fills
// it: phase increment >> 16, so that 0x800 is the model's own tuning. The synth
// path holds full increments; C4 (25480119 at 44.1 kHz) maps to 0x800, as the
// Q31 product increment * (0x800 << 31 / 25480119).
const u32 kPitch = 0x11902c00;
const i32 kC4Scale = 172607;

// Level, as stock plays drumline. Render scales each sample by the per-voice
// gain the alternate renderer fills at 0x11903400 and shifts it left by 7,
// saturating. The synth path applies volume and velocity after the voice, so the
// binding renders at full gain, and the synth path scales the voice by its
// engine's level method +40 (a synth's default gives about 1/32), so the
// machine's +40 returns drumline's own level. Measured on the device, a kick so
// rendered peaks within 0.1 dB of stock drumline's at the same track volume,
// with no saturation at this or any lower gain.
const u32 kGain = 0x11903400;
const u32 kFullGain = 0x7fffffff;
const u32 kLevel = 0x8d513a08;

bool ready(const Voice* voice) { return voice->words[0] == kVtable; }

void block(const Patch& patch, i32 (&out)[8]) {
    const u32 model = patch.model < opxy::drumline::models ? patch.model : 0;
    out[0] = static_cast<i32>(model * 6 * kModelStep + kModelCentre);
    for (u32 i = 0; i < 4; ++i) out[1 + i] = patch.controls[i] < 0 ? 0 : patch.controls[i];
    out[5] = kCentre;
    out[6] = patch.random < 0 ? 0 : patch.random;
    out[7] = kUnityGain;
}
}  // namespace

extern "C" {
// The stock-voice synth's method +40 (voice.cpp.in names it).
i32 sdk_drumline_level(const void*) __asm__("sdk_drumline_level");
i32 sdk_drumline_level(const void*) {
    return stock<i32(const void*)>(kLevel)(nullptr);
}

void sdk_drumline_construct(Voice* voice) {
    for (u32 i = 0; i < 140; ++i) voice->words[i] = 0;
    stock<void(Voice*)>(kConstruct)(voice);
}

void sdk_drumline_start(Voice* voice, const Patch* patch) {
    if (!ready(voice)) return;
    voice->words[kKeyWord] = kKey;
    voice->words[kLengthOffsetWord] = kCentre;
    stock<void(Voice*)>(kStart)(voice);
    (void)patch;
}

void sdk_drumline_render(Voice* voice, const Patch* patch, i32* output, const i32* parameters,
                         const u32* increments) {
    if (!ready(voice)) {
        for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
    }
    i32 words[8];
    block(*patch, words);
    if (patch->keep_pitch) voice->words[kPitchRandomWord] = 0;
    // Track words 0..7 offset the controls about 0.5: neutral, so the machine's
    // patch sets them. Words 8..11 are a synth's amp envelope (attack, decay,
    // sustain, release); on a drumline track they are its funk, attack, random
    // and length, which render reads as its attack (9), random added squared (10)
    // and a length offset about 0.5 (11). So the envelope's attack drives
    // drumline's attack and its decay drumline's length; funk and the added
    // random stay off. Sustain and release act as on any synth, in the amp
    // envelope stock applies after the voice.
    // Audio renders one voice at a time, so scratch is static: stock's own
    // drumline frame already sits on the audio stack.
    static i32 track[kTrackWords];
    static u32 saved[opxy::block_frames], saved_gain[opxy::block_frames];
    for (u32 i = 0; i < 8; ++i) track[i] = kCentre;
    for (u32 i = 8; i < kTrackWords; ++i) track[i] = 0;
    track[kDrumlineAttack] = parameters[kAmpAttack];
    track[kDrumlineLength] = parameters[kAmpDecay];
    u32* pitch = reinterpret_cast<u32*>(kPitch);
    u32* gain = reinterpret_cast<u32*>(kGain);
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        saved[i] = pitch[i];
        saved_gain[i] = gain[i];
        gain[i] = kFullGain;
        pitch[i] = static_cast<u32>(opxy::q31::multiply(static_cast<i32>(increments[i]), kC4Scale));
    }
    stock<void(Voice*, i32*, const i32*, const i32*)>(kRender)(voice, output, words, track);
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        pitch[i] = saved[i];
        gain[i] = saved_gain[i];
    }
}
}
