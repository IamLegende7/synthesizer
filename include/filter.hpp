#include <algorithm>
#include <cmath>

#include "envelope.hpp"

struct Filter {
    enum FilterType {Lowpass, Highpass, Bandpass, Notch};
    FilterType type = FilterType::Lowpass;
    float cutoff_hz = 2400.0f;
    float resonance = 0.35f;
    float env_amount = 0.50f;
    float lfo_amount = 0.0f;
    float key_tracking = 0.50f;
    ADSREnvelope envelope{};
};

// Place after struct Filter.
class StateVariableFilter {
private:
    // Integrator state; keep these between samples.
    double ic1eq_ = 0.0;
    double ic2eq_ = 0.0;

public:
    float process(float input,
                  float cutoff_hz,
                  float resonance,
                  Filter::FilterType type,
                  float sample_rate) {
        if (sample_rate <= 0.0f)
            return input;

        const double sr = sample_rate;
        const double cutoff = std::clamp(
            static_cast<double>(cutoff_hz),
            1.0,
            sr * 0.45);

        // Interpret resonance as 0..1, with higher values increasing resonance.
        const double res = std::clamp(
            static_cast<double>(resonance), 0.0, 1.0);
        const double k = std::max(0.05, 2.0 * (1.0 - res));

        const double g = std::tan(std::acos(-1.0) * cutoff / sr);
        const double a1 = 1.0 / (1.0 + g * (g + k));
        const double a2 = g * a1;
        const double a3 = g * a2;

        const double v3 = static_cast<double>(input) - ic2eq_;
        const double band = a1 * ic1eq_ + a2 * v3;
        const double low = ic2eq_ + a2 * ic1eq_ + a3 * v3;
        const double high = static_cast<double>(input) - k * band - low;

        ic1eq_ = 2.0 * band - ic1eq_;
        ic2eq_ = 2.0 * low - ic2eq_;

        switch (type) {
            case Filter::Lowpass:
                return static_cast<float>(low);
            case Filter::Highpass:
                return static_cast<float>(high);
            case Filter::Bandpass:
                return static_cast<float>(band);
            case Filter::Notch:
                return static_cast<float>(high + low);
        }

        return static_cast<float>(low);
    }

    void Reset() {
        ic1eq_ = 0.0;
        ic2eq_ = 0.0;
    }
};