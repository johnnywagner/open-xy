// Stock drumline voices inside a synth machine (resources.stockVoice "drumline").
//
// The machine owns each drumline voice and its whole patch; the user's own
// stock OS renders it. The SDK only calls stock's drumline code at fixed,
// profile-pinned addresses in the firmware the build patches: no stock code or
// data is part of a machine or of this SDK. Host builds (machine tests, the
// browser preview and the CPU estimate) have no firmware: voices there are
// silent and cost nothing, so judge sound and CPU on the device.
#pragma once
#include <opxy/audio.hpp>

namespace opxy { namespace drumline {

// Stock's model families, in stock order: kick, snare, hihat, shaker, clap,
// congas, fm, fm slap, pitch, tambourine, resonator.
const u32 models = 11;

// Stock's voice object, opaque. Put it in the machine's Voice, word aligned.
struct Voice { u32 words[140]; };

// One voice's sound, set at every start and render. The four controls are the
// model's own (stock's per-model param 1..4: the first is its pitch or colour,
// the second its length); random scales stock's per-hit variation. Controls and
// random are Q31 0..max; stock clamps each to the model's range.
struct Patch {
    u32 model;         // 0..models-1
    i32 random;
    i32 controls[4];
    bool keep_pitch;   // true: random leaves the first control (pitch) alone
};

// Prepares the voice; call once from initialize. A voice that is not
// constructed, or whose construction stock did not confirm, renders silence.
void construct(Voice& voice);
// Starts a hit: stock's note start for this voice and patch.
void start(Voice& voice, const Patch& patch);
// One mono block into output (block_frames Q31 samples, at stock's drumline
// level). parameters and increments are the synth render's own: pitch follows
// the played note, with middle C (C4) at the model's own tuning.
void render(Voice& voice, const Patch& patch, i32* output, const i32* parameters, const u32* increments);

}}  // namespace opxy::drumline

#if defined(__bfin__)
extern "C" {
void sdk_drumline_construct(opxy::drumline::Voice*) __asm__("sdk_drumline_construct");
void sdk_drumline_start(opxy::drumline::Voice*, const opxy::drumline::Patch*) __asm__("sdk_drumline_start");
void sdk_drumline_render(opxy::drumline::Voice*, const opxy::drumline::Patch*, opxy::i32*, const opxy::i32*,
                         const opxy::u32*) __asm__("sdk_drumline_render");
}
namespace opxy { namespace drumline {
inline void construct(Voice& voice) { sdk_drumline_construct(&voice); }
inline void start(Voice& voice, const Patch& patch) { sdk_drumline_start(&voice, &patch); }
inline void render(Voice& voice, const Patch& patch, i32* output, const i32* parameters, const u32* increments) {
    sdk_drumline_render(&voice, &patch, output, parameters, increments);
}
}}
#else
// Host builds: no firmware, so silence.
namespace opxy { namespace drumline {
inline void construct(Voice& voice) { for (u32 i = 0; i < 140; ++i) voice.words[i] = 0; }
inline void start(Voice&, const Patch&) {}
inline void render(Voice&, const Patch&, i32* output, const i32*, const u32*) {
    for (u32 i = 0; i < block_frames; ++i) output[i] = 0;
}
}}
#endif
