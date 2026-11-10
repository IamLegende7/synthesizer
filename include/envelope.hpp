#pragma once

#include <cmath>

struct ADSREnvelope {
    enum Stage {Idle, Attack, Decay, Sustain, Release};
    Stage stage = Stage::Idle;
    float value = 0.0f;
    float release_start = 0.0f;

    float attack  = 5.0f;
    float decay   = 180.0f;
    float sustain = 0.70f;
    float release = 300.0f;

    ADSREnvelope(float attack = 5.0f, float decay = 180.0f, float sustain = 0.70f, float release = 300.0f, float sample_rate = 48000.0f): 
        attack(std::max(1.0f, attack * sample_rate / 1000.0f)),
        decay(std::max(1.0f, decay * sample_rate / 1000.0f)),
        sustain(std::max(0.0f, std::min(1.0f, sustain))),
        release(std::max(1.0f, release * sample_rate / 1000.0f)) {};

    void NoteOn() {
        stage = Stage::Attack;
    }

    void NoteOff() {
        if (stage != Stage::Idle) {
            release_start = value;
            stage = Stage::Release;
        }
    }

    float Next() {
        if (stage == Stage::Idle) {
            value = 0.0f;
        } else if (stage == Stage::Attack) {
            value += 1.0f / attack;
            if (value >= 1.0f) {
                value = 1.0f;
                stage = Stage::Decay;
            }
        } else if (stage == Stage::Decay) {
            value -= (1.0f - sustain) / decay;
            if (value <= sustain) {
                value = sustain;
                stage = Stage::Sustain;
            }
        } else if (stage == Stage::Sustain) {
            value = sustain;
        } else if (stage == Stage::Release) {
            value -= release_start / release;
            if (value <= 0.0f) {
                value = 0.0f;
                stage = Stage::Idle;
            }
        }
        return value;
    }

    bool IsIdle() const {
        return (stage == Stage::Idle);
    }
};