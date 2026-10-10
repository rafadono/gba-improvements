#pragma once

#include <cstddef>
#include <cstdint>

namespace gbaimp {

// Applies HD-track mixing followed by the DSP chain to an interleaved
// stereo int16 buffer (sample_count = total int16 values). Call it from
// your SDL_QueueAudio interception before handing data to the device.
void process_audio_block(int16_t* interleaved, size_t sample_count);

} // namespace gbaimp
