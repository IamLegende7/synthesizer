#include "synth.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {

float clampFloat(float value, float low, float high) {
    return std::max(low, std::min(high, value));
}

int clampInt(int value, int low, int high) {
    return std::max(low, std::min(high, value));
}

Oscillator::Waveform parseWaveform(const std::string& name) {
    if (name == "SINE")     return Oscillator::Sine;
    if (name == "SQUARE")   return Oscillator::Square;
    if (name == "SAW")      return Oscillator::Saw;
    if (name == "TRIANGLE") return Oscillator::Triangle;
    if (name == "NOISE")    return Oscillator::Noise;
    if (name == "RANDOM")   return Oscillator::Random;

    throw std::runtime_error("Ungültige Oszillator-Wellenform: " + name);
}

Filter::FilterType parseFilterType(const std::string& name) {
    if (name == "LOWPASS")  return Filter::Lowpass;
    if (name == "HIGHPASS") return Filter::Highpass;
    if (name == "BANDPASS") return Filter::Bandpass;
    if (name == "NOTCH")    return Filter::Notch;

    throw std::runtime_error("Ungültiger Filtertyp: " + name);
}

} // namespace

void Synthesizer::LoadJson(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Preset-Datei kann nicht geöffnet werden: " + path);
    }

    const json root = json::parse(file);

    if (!root.contains("oscillators") || !root["oscillators"].is_array() ||
        root["oscillators"].size() != oscillator_count) {
        throw std::runtime_error("Preset muss genau 3 Oszillatoren enthalten.");
    }

    // Load into temporary values first, so a failed parse does not partially
    // replace the current synthesizer settings.
    auto newOscillators = oscillators;
    auto newFilter = filter;
    auto newLfo = lfo;
    auto newAmpEnvelope = amp_envelope_config;
    float newMasterLevelDb = master_level_db;
    bool newRingMod = ring_mod;

    for (std::size_t i = 0; i < oscillator_count; ++i) {
        const auto& source = root["oscillators"][i];
        auto& target = newOscillators[i];

        target.enabled = source.value("enabled", true);
        target.waveform = parseWaveform(source.value("waveform", std::string("SINE")));
        target.transpose = clampInt(source.value("transpose", 0), -36, 36);
        target.offset_cents = clampFloat(source.value("offsetCents", 0.0f), -100.0f, 100.0f);
        target.db_level = clampFloat(source.value("levelDb", -6.0f), -60.0f, 6.0f);
        target.pulse_width = clampFloat(source.value("pulseWidth", 0.5f), 0.01f, 0.99f);
        target.lfo_pitch_cents =
            clampFloat(source.value("lfoPitchCents", 0.0f), -1200.0f, 1200.0f);
        target.env_pitch_cents =
            clampFloat(source.value("envPitchCents", 0.0f), -2400.0f, 2400.0f);
        target.warm = source.value("warm", false);
    }

    const auto& amp = root.at("amp");
    const float ampAttack = clampFloat(amp.value("attackMs", 5.0f), 0.0f, 10000.0f);
    const float ampDecay = clampFloat(amp.value("decayMs", 180.0f), 0.0f, 10000.0f);
    const float ampSustain = clampFloat(amp.value("sustain", 0.70f), 0.0f, 1.0f);
    const float ampRelease = clampFloat(amp.value("releaseMs", 300.0f), 0.0f, 20000.0f);

    // The current Voice implementation uses a 48 kHz sample rate.
    newAmpEnvelope = ADSREnvelope(
        ampAttack, ampDecay, ampSustain, ampRelease, 48000.0f);

    newMasterLevelDb =
        clampFloat(root.value("masterLevelDb", 0.0f), -60.0f, 6.0f);

    const auto& filterJson = root.at("filter");
    newFilter.type = parseFilterType(
        filterJson.value("type", std::string("LOWPASS")));
    newFilter.cutoff_hz =
        clampFloat(filterJson.value("cutoffHz", 2400.0f), 20.0f, 20000.0f);
    newFilter.resonance =
        clampFloat(filterJson.value("resonance", 0.35f), 0.0f, 1.0f);
    newFilter.env_amount =
        clampFloat(filterJson.value("envAmount", 0.50f), -1.0f, 1.0f);
    newFilter.lfo_amount =
        clampFloat(filterJson.value("lfoAmount", 0.0f), -1.0f, 1.0f);
    newFilter.key_tracking =
        clampFloat(filterJson.value("keyTracking", 0.50f), 0.0f, 1.0f);

    const auto& filterEnv = filterJson.at("envelope");
    const float filterAttack =
        clampFloat(filterEnv.value("attackMs", 5.0f), 0.0f, 10000.0f);
    const float filterDecay =
        clampFloat(filterEnv.value("decayMs", 180.0f), 0.0f, 10000.0f);
    const float filterSustain =
        clampFloat(filterEnv.value("sustain", 0.70f), 0.0f, 1.0f);
    const float filterRelease =
        clampFloat(filterEnv.value("releaseMs", 300.0f), 0.0f, 20000.0f);

    newFilter.envelope = ADSREnvelope(
        filterAttack, filterDecay, filterSustain, filterRelease, 48000.0f);

    const auto& lfoJson = root.at("lfo");
    newLfo.waveform = parseWaveform(lfoJson.value("waveform", std::string("SINE")));
    newLfo.frequency = clampFloat(lfoJson.value("rateHz", 5.0f), 0.01f, 40.0f);
    newLfo.delay_ms = clampFloat(lfoJson.value("delayMs", 0.0f), 0.0f, 10000.0f);
    newLfo.retrigger = lfoJson.value("retrigger", false);
    newLfo.enabled = true;

    newRingMod = root.value("ringMod", false);

    // Commit after parsing and validation succeeded.
    oscillators = newOscillators;
    filter = newFilter;
    lfo = newLfo;
    amp_envelope_config = newAmpEnvelope;
    master_level_db = newMasterLevelDb;
    ring_mod = newRingMod;

    // Existing voices have already copied their envelope settings.
    voices.clear();
    lfo_phase = 0.0f;
}