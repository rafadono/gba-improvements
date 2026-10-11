#pragma once

#include <cstddef>
#include <cstdint>

namespace gbaimp {

// Applies HD-track mixing followed by the DSP chain to an interleaved
// stereo int16 buffer (sample_count = total int16 values) at the rate configured in
// HdAudioSettings::target_sample_rate. Call it from your SDL_QueueAudio interception
// before handing data to the device. Equivalent to the 4-argument form with
// channels = 2 and that rate.
void process_audio_block(int16_t* interleaved, size_t sample_count);

// Channel- and rate-aware form. `sample_count` is the total number of int16 values in
// the buffer; `channels` is 1 (mono) or 2 (interleaved stereo), anything else leaves
// the buffer untouched. `sample_rate` is the real rate of the buffer (it drives the
// DSP coefficients and the HD track resampling); a value <= 0 means
// HdAudioSettings::target_sample_rate. Mono buffers get the HD track folded down to
// mono and AudioDsp::process_mono; stereo width does not apply to mono.
void process_audio_block(int16_t* samples, size_t sample_count, int channels, int sample_rate);

} // namespace gbaimp
