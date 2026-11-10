#ifndef SYNTH_HPP
#define SYNTH_HPP

#include <SDL3/SDL.h>
#include <array>
#include <vector>
#include <cmath>
#include <string>
#include <cstddef>

#include "stream.hpp"
#include "envelope.hpp"
#include "filter.hpp"

constexpr Uint8 oscillator_count = 3;

struct Oscillator {
    enum Waveform { Sine, Square, Saw, Triangle, Noise, Random };

    Waveform waveform = Sine;
    bool enabled = true;
    bool retrigger = false;      // Used by the LFO
    float frequency = 0.0f;
    float gain = 0.0f;
    bool warm = false;
    float delay_ms = 0.0f;
    int transpose = 0;
    float offset_cents = 0.0f;
    float lfo_pitch_cents = 0.0f;
    float env_pitch_cents = 0.0f;
    float db_level = -6.0f;
    float pulse_width = 0.5f;

    float Get(float phase) const {
        phase -= std::floor(phase); // Wrap to [0, 1)

        switch (waveform) {
            case Sine:
                return std::sin(2.0f * SDL_PI_F * phase);
            case Square:
                return phase < pulse_width ? 1.0f : -1.0f;
            case Saw:
                return 2.0f * phase - 1.0f;
            case Triangle:
                return 1.0f - 4.0f * std::abs(phase - 0.5f);
            case Noise:
            case Random:
                return 0.0f; // Noise is generated separately in Voice::Next()
        }
        return 0.0f;
    }
};

class Synthesizer;

class Voice {
    private:
        Synthesizer* synth;
        float sample_rate = 48000.0f;

        std::array<float, static_cast<int>(oscillator_count)> phases{0};
        std::array<float, static_cast<int>(oscillator_count)> warm_phases{0};

        ADSREnvelope amp_envelope{};
        ADSREnvelope filter_envelope{};
        StateVariableFilter filterL{};

        float lfo_phase = 0.0f;
        float last_random_phase = 0.0f;
        float random_lfo = 0.0f;
        Sint64 age_samples = 0;
    public:
        int note;
        float velocity;
        bool released = false;

        Voice(Synthesizer* synth, int note, float velocity);
        bool IsFinished() const;
        void NoteOff();
        float Next();
        float LFOValue(float phase);
        float dbToLinear(float db);
        float Wrap01(float x);
};

class SynthDisplay;

class Synthesizer : public AudioPlayer {
    friend class Voice;
    friend class SynthDisplay;

    private:
        static constexpr std::size_t display_history_size = 1024;

        // Index 0: combined oscillator mix. Indices 1–3: individual oscillators.
        std::array<std::array<float, display_history_size>, 4> display_history_{};
        std::array<float, 4> display_work_{};
        std::size_t display_write_ = 0;
        std::size_t display_count_ = 0;

        Oscillator lfo;
        float lfo_phase = 0.0f;
        std::vector<Voice> voices;
        static constexpr std::size_t max_voices = 32;
        ADSREnvelope amp_envelope_config{};

    public:
        std::array<Oscillator, static_cast<std::size_t>(oscillator_count)> oscillators{};
        Filter filter{};
        bool ring_mod = false;
        float master_level_db = 0.0f;

        void NoteOn(int note, int velocity);
        void NoteOff(int note);
        void AllNotesOff();
        void LoadJson(const std::string& path);

        void Callback(int sample_count, AudioBuffer* output) override;
};

#endif