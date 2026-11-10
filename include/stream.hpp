#ifndef STREAM_HPP
#define STREAM_HPP

#include <SDL3/SDL.h>
#include <array>
#include <functional>
#include <atomic>

constexpr Uint32 buffer_sample_count = 512;

typedef std::array<float, static_cast<int>(buffer_sample_count)> AudioBuffer;

class AudioStream;

class AudioPlayer {
    public:
        float volume = 1.0f; // 0 .. 1
        AudioStream* parent = nullptr;
        virtual void Callback(int needed_bytes, AudioBuffer* output); // Needed bytes is never > buffer_sample_count
};

class AudioStream {
    private:
        SDL_AudioStream* stream;
        std::vector<AudioPlayer*> audio_players;

        static void SDLCALL Callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount);
        void Generate(SDL_AudioStream* stream, int needed_bytes);

        static constexpr std::size_t chorus_delay_size = 1024; // Longer than the 610-sample maximum delay
        std::array<float, chorus_delay_size> chorus_delay{};
        std::size_t chorus_index = 0;
        double chorus_phase = 0.0;
        std::atomic_bool chorus_enabled{false};
    public:
        float sample_rate;
        bool playing = false;
        
        AudioStream(const AudioStream&) = delete;
        AudioStream& operator=(const AudioStream&) = delete;

        AudioStream();
        ~AudioStream();
        bool Init(float sample_rate = 48000.0f);

        void AddPlayer(AudioPlayer* audio_player);
        void RemovePlayer(AudioPlayer* audio_player);

        void Lock();
        void Unlock();

        void SetPlayback(bool value);
        bool TogglePlayback();

        void SetChorusEnabled(bool enabled) {
           chorus_enabled.store(enabled);
        }
};

class AudioStreamGuard {
    public:
        explicit AudioStreamGuard(AudioStream* stream) : stream_(stream) {
            if (stream_) stream_->Lock();
        }

        ~AudioStreamGuard() {
            if (stream_) stream_->Unlock();
        }

        AudioStreamGuard(const AudioStreamGuard&) = delete;
        AudioStreamGuard& operator=(const AudioStreamGuard&) = delete;

    private:
        AudioStream* stream_;
};

#endif