#include "synth.hpp"
#include <cmath>

Voice::Voice(Synthesizer* synth, int note, float velocity): synth(synth), amp_envelope(synth->amp_envelope_config), filter_envelope(synth->filter.envelope), note(note), velocity(velocity) {
    amp_envelope.NoteOn();
    filter_envelope.NoteOn();
};

bool Voice::IsFinished() const {
    return amp_envelope.IsIdle();
}

void Voice::NoteOff() {
    if (!released) {
        released = true;
        amp_envelope.NoteOff();
        filter_envelope.NoteOff();
    }
}

float Voice::Next() {
    float amp = amp_envelope.Next();
    const float fenv = filter_envelope.Next();
    if (IsFinished())
        return 0.0f;

    float final_lfo_phase = synth->lfo.retrigger
        ? lfo_phase
        : synth->lfo_phase;
    float lfo = LFOValue(final_lfo_phase);
    float delay_samples = synth->lfo.delay_ms * sample_rate / 1000.0;
    float lfo_fade = (delay_samples <= 0)
        ? 1.0f
        : std::min(1.0f, std::max(0.0f, (age_samples - delay_samples) / std::max(1.0f, delay_samples * 0.25f)));
    if (age_samples < delay_samples)
        lfo_fade = 0.0f;
    lfo *= lfo_fade;

    std::array<float, static_cast<int>(oscillator_count)> osc_values{};
    for (int i = 0; i < 3; i++) {
        osc_values[i] = 0.0f;
        Oscillator& osc = synth->oscillators[i];
        if (!osc.enabled) continue;

        float semis = (note - 69) + osc.transpose + (osc.offset_cents + lfo * osc.lfo_pitch_cents + amp * osc.env_pitch_cents) / 100.0f;
        float freq = 440.0 * std::pow(2.0f, semis / 12.0f);
        osc_values[i] = (osc.waveform == Oscillator::Waveform::Noise)
            ? SDL_randf() * 2.0f - 1.0f
            : osc.Get(phases[i]);
        phases[i] = Wrap01(phases[i] + freq / sample_rate);
        if (osc.warm) {
            float warm = osc.Get(warm_phases[i]);
            warm_phases[i] = Wrap01(warm_phases[i] + freq * 1.003f / sample_rate);
            osc_values[i] = 0.78f * osc_values[i] + 0.22f * warm;
        }
        osc_values[i] *= dbToLinear(osc.db_level);
    }

    float mixed;
    if (synth->ring_mod && synth->oscillators[0].enabled && synth->oscillators[1].enabled) {
        mixed = osc_values[0] * osc_values[1] + osc_values[2];
    } else {
        mixed = osc_values[0] + osc_values[1] + osc_values[2];
    }
    mixed *= 0.45;

    const float display_gain = amp * velocity * 0.45f;
    for (int i = 0; i < 3; ++i)
        synth->display_work_[i + 1] += osc_values[i] * display_gain;
    synth->display_work_[0] += mixed * display_gain;

    const Filter& fp = synth->filter;
    const double key_ratio = std::pow(2.0, (static_cast<double>(note) - 60.0) / 12.0 * fp.key_tracking);

    double cutoff = fp.cutoff_hz * key_ratio;
    cutoff *= std::pow(2.0, fp.env_amount * fenv * 5.0);
    cutoff *= std::pow(2.0, fp.lfo_amount * lfo * 3.0);

    const float filtered = filterL.process(mixed, static_cast<float>(cutoff), fp.resonance, fp.type, sample_rate);

    age_samples++;
    if (synth->lfo.retrigger)
        lfo_phase = Wrap01(lfo_phase + synth->lfo.frequency / sample_rate);

    return filtered * amp * velocity;
}

float Voice::LFOValue(float phase) {
    phase = Wrap01(phase);
    switch (synth->lfo.waveform) {
        case Oscillator::Waveform::Sine:
            return std::sin(2.0f * SDL_PI_F * phase);
        case Oscillator::Waveform::Triangle:
            return 1.0f - 4.0f * std::abs(phase - 0.5f);
        case Oscillator::Waveform::Saw:
            return 2.0f * phase - 1.0f;
        case Oscillator::Waveform::Square:
            return phase < 0.5f ? 1.0f : -1.0f;
        case Oscillator::Waveform::Noise:
            return SDL_randf() * 2.0f - 1.0f;
        case Oscillator::Waveform::Random:
            if (phase < last_random_phase)
                random_lfo = SDL_randf() * 2.0f - 1.0f;
            last_random_phase = phase;
            return random_lfo;
            break;
    }
    return -1.0f;
}

float Voice::dbToLinear(float db) {
    return std::pow(10.0, db / 20.0);
}

float Voice::Wrap01(float x) {
    return x - std::floor(x);
}




void Synthesizer::NoteOn(int note, int velocity) {
    //SDL_Log("NoteOn(%d, %d)", note, velocity);
    if (velocity <= 0) {
        NoteOff(note);
        return;
    }

    AudioStreamGuard guard(parent);
    if (voices.size() >= max_voices)
        voices.erase(voices.begin()); // Same oldest-voice policy as the Java code

    const float normalized_velocity =
        std::max(0.0f, std::min(1.0f, velocity / 127.0f));

    voices.emplace_back(this, note, normalized_velocity);
}

void Synthesizer::NoteOff(int note) {
    AudioStreamGuard guard(parent);
    for (Voice& voice : voices) {
        if (voice.note == note)
            voice.NoteOff();
    }
}

void Synthesizer::AllNotesOff() {
    AudioStreamGuard guard(parent);
    for (Voice& voice : voices)
        voice.NoteOff();
}

void Synthesizer::Callback(int sample_count, AudioBuffer* output) {
    if (!output)
        return;

    output->fill(0.0f);
    const int count = std::max(0, std::min(static_cast<int>(buffer_sample_count), sample_count));

    const float master_gain = std::pow(10.0f, master_level_db / 20.0f);
    const float rate = (parent && parent->sample_rate > 0.0f)
        ? parent->sample_rate
        : 48000.0f;

    for (int i = 0; i < count; ++i) {
        display_work_.fill(0.0f);

        float sum = 0.0f;
        for (Voice& voice : voices)
            sum += voice.Next();

        (*output)[i] = std::tanh(sum) * master_gain;

        for (std::size_t trace = 0; trace < display_history_.size(); ++trace)
            display_history_[trace][display_write_] = display_work_[trace];

        display_write_ = (display_write_ + 1) % display_history_size;
        display_count_ = std::min(display_count_ + 1, display_history_size);

        lfo_phase += lfo.frequency / rate;
        lfo_phase -= std::floor(lfo_phase);
    }

    voices.erase(
        std::remove_if(voices.begin(), voices.end(),
            [](const Voice& voice) { return voice.IsFinished(); }),
        voices.end());
}