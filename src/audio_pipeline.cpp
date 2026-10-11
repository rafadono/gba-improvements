#include "gbaimp/audio_pipeline.h"
#include "gbaimp/audio_dsp.h"
#include "gbaimp/hd_audio_player.h"

namespace gbaimp {

void process_audio_block(int16_t* interleaved, size_t sample_count) {
    const int sample_rate = HdAudioPlayer::instance().settings().target_sample_rate;
    process_audio_block(interleaved, sample_count, 2, sample_rate);
}

void process_audio_block(int16_t* samples, size_t sample_count, int channels, int sample_rate) {
    if (!samples || sample_count == 0) return;
    if (channels != 1 && channels != 2) return;   // unsupported layout: leave untouched
    if (sample_rate <= 0) sample_rate = HdAudioPlayer::instance().settings().target_sample_rate;

    HdAudioPlayer::instance().mix_audio(samples, sample_count, channels, sample_rate);
    if (channels == 1) {
        AudioDsp::instance().process_mono(samples, sample_count, sample_rate);
    } else {
        AudioDsp::instance().process_stereo(samples, sample_count / 2, sample_rate);
    }
}

} // namespace gbaimp
