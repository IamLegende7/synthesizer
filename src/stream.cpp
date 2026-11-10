#include "stream.hpp"

#include <algorithm>
#include <cmath>

void AudioPlayer::Callback(int needed_bytes, AudioBuffer* output) {
    SDL_Log("AudioPlayer::Callback should be replaced by a child, not called directly!");
    for (int i = 0; i < needed_bytes; ++i) {
        (*output)[i] = 0.0f;
    }
}



void AudioStream::Generate(SDL_AudioStream* stream, int needed_bytes) {
    constexpr int channels = 2;
    constexpr int bytes_per_frame = channels * static_cast<int>(sizeof(float));

    int frames_left = needed_bytes / bytes_per_frame;

    while (frames_left > 0) {
        const int count = std::min(
            frames_left, static_cast<int>(buffer_sample_count));

        AudioBuffer mixed_buffer{};

        for (AudioPlayer* player : audio_players) {
            AudioBuffer tmp_buffer{};
            player->Callback(count, &tmp_buffer);

            for (int i = 0; i < count; ++i)
                mixed_buffer[i] += tmp_buffer[i] * player->volume;
        }

        std::array<float, buffer_sample_count * channels> stereo_buffer{};
        const bool chorus = chorus_enabled.load();

        for (int i = 0; i < count; ++i) {
            const float sample = mixed_buffer[i];
            chorus_delay[chorus_index] = sample;

            const double mod = (std::sin(chorus_phase) + 1.0) * 0.5;
            const int delay_l = 350 + static_cast<int>(mod * 180.0);
            const int delay_r = 430 + static_cast<int>((1.0 - mod) * 180.0);

            const auto delayed_sample = [this](int delay) {
                const std::size_t index =
                    (chorus_index + chorus_delay_size -
                     static_cast<std::size_t>(delay)) % chorus_delay_size;
                return chorus_delay[index];
            };

            const float wet_l = chorus ? delayed_sample(delay_l) : sample;
            const float wet_r = chorus ? delayed_sample(delay_r) : sample;

            const float left = chorus ? 0.75f * sample + 0.25f * wet_l : sample;
            const float right = chorus ? 0.75f * sample + 0.25f * wet_r : sample;

            // Java clips here, after chorus and master gain have been applied.
            stereo_buffer[i * 2] =
                std::clamp(left, -1.0f, 1.0f);
            stereo_buffer[i * 2 + 1] =
                std::clamp(right, -1.0f, 1.0f);

            chorus_index = (chorus_index + 1) % chorus_delay_size;
            chorus_phase += 2.0 * SDL_PI_D * 0.28 / sample_rate;
            if (chorus_phase > 2.0 * SDL_PI_D)
                chorus_phase -= 2.0 * SDL_PI_D;
        }

        SDL_PutAudioStreamData(
            stream,
            stereo_buffer.data(),
            count * bytes_per_frame);

        frames_left -= count;
    }
}

void SDLCALL AudioStream::Callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int /*total_amount*/) {
    auto* self = static_cast<AudioStream*>(userdata);
    self->Generate(stream, additional_amount);
}

AudioStream::AudioStream() {

}

AudioStream::~AudioStream() {
    if (this->stream)
        SDL_DestroyAudioStream(this->stream);
}

bool AudioStream::Init(float sample_rate) {
    this->sample_rate = sample_rate;
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = static_cast<int>(this->sample_rate);

    this->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, this->Callback, this);

    if (!this->stream) {
        SDL_Log("Could not open audio stream: %s", SDL_GetError());
        return false;
    }
    return true;
}

void AudioStream::AddPlayer(AudioPlayer* audio_player) {
    audio_player->parent = this;
    this->audio_players.push_back(audio_player);
}

void AudioStream::RemovePlayer(AudioPlayer* audio_player) {
    if (!audio_player) {
        return;
    }

    AudioStreamGuard guard(this);

    audio_players.erase(
        std::remove(audio_players.begin(), audio_players.end(), audio_player),
        audio_players.end());

    if (audio_player->parent == this) {
        audio_player->parent = nullptr;
    }
}

void AudioStream::Lock() {
    SDL_LockAudioStream(this->stream);
}

void AudioStream::Unlock() {
    SDL_UnlockAudioStream(this->stream);
}

void AudioStream::SetPlayback(bool value) {
    if (!this->playing && value)
        SDL_ResumeAudioStreamDevice(this->stream);
    if (this->playing && !value)
        SDL_PauseAudioStreamDevice(this->stream);
    this->playing = value;
}

bool AudioStream::TogglePlayback() {
    SetPlayback(!this->playing);
    return this->playing;
}