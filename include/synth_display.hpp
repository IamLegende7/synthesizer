#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "midi_player.hpp"

class SynthDisplay {
public:
    // X > 1 shows a shorter, more recent time span.
    // Y > 1 increases the displayed waveform amplitude.
    static constexpr float OVERVIEW_X_MULT = 1.0f;
    static constexpr float OVERVIEW_Y_MULT = 6.0f;

    static constexpr float OSCILLATORS_X_MULT = 1.0f;
    static constexpr float OSCILLATORS_Y_MULT = 5.0f;

    static constexpr float LEGEND_WIDTH = 300.0f;

    void Init(SDL_Renderer* renderer, MidiPlayer* player, Synthesizer* standalone = nullptr) {
        this->renderer = renderer;
        this->player = player;
        this->standalone = standalone;
    }

    void ReDraw(SDL_FRect bounds) {
        std::vector<TraceSet> snapshots;

        // Copy histories while the audio callback is paused.
        {
            AudioStreamGuard guard(player->stream_);
            if (standalone)
                CopySynth(standalone, "Synth", snapshots);
            if (player)
                for (auto& track : player->tracks_)
                    CopySynth(track.synth.get(), track.instrument, snapshots);
        }

        legendWidth = std::min(LEGEND_WIDTH, bounds.w * 0.3f);
        DrawLegend(renderer, bounds, legendWidth, snapshots);
    }

    void Draw(SDL_FRect bounds) const {
        if (!renderer || bounds.w <= 0.0f || bounds.h <= 0.0f)
            return;

        std::vector<TraceSet> snapshots;

        // Copy histories while the audio callback is paused.
        {
            AudioStreamGuard guard(player->stream_);

            if (standalone)
                CopySynth(standalone, "Synth", snapshots);
            if (player)
                for (auto& track : player->tracks_)
                    CopySynth(track.synth.get(), track.instrument, snapshots);
        }

        SDL_FRect plotBounds = bounds;
        plotBounds.x += legendWidth;
        plotBounds.w -= legendWidth;

        if (plotBounds.w <= 0.0f)
            return;

        constexpr float gap = 8.0f;
        const float overviewHeight = plotBounds.h * 0.64f;
        const float oscillatorY = plotBounds.y + overviewHeight + gap;
        const float oscillatorHeight = plotBounds.y + plotBounds.h - oscillatorY;
        const float oscillatorWidth = (plotBounds.w - 2.0f * gap) / 3.0f;

        DrawPanel(renderer,
                  {plotBounds.x, plotBounds.y, plotBounds.w, overviewHeight},
                  "Combined", 0, OVERVIEW_X_MULT, OVERVIEW_Y_MULT,
                  snapshots);

        for (int osc = 0; osc < 3; ++osc) {
            const std::string title = "Oscillator " + std::to_string(osc + 1);

            DrawPanel(renderer,
                      {plotBounds.x + osc * (oscillatorWidth + gap),
                       oscillatorY, oscillatorWidth, oscillatorHeight},
                      title.c_str(),
                      static_cast<std::size_t>(osc + 1),
                      OSCILLATORS_X_MULT, OSCILLATORS_Y_MULT,
                      snapshots);
        }
    }

private:
    SDL_Renderer* renderer;
    MidiPlayer* player;
    Synthesizer* standalone;
    float legendWidth = 100.0f;

    struct TraceSet {
        std::string name;
        std::array<std::vector<float>, 4> traces;
    };

    void CopySynth(Synthesizer* synth, const std::string& name, std::vector<TraceSet>& snapshots) const {
        if (!synth)
            return;

        TraceSet snapshot;
        snapshot.name = name;

        const std::size_t count = synth->display_count_;
        const std::size_t start =
            (synth->display_write_ +
                Synthesizer::display_history_size - count) %
            Synthesizer::display_history_size;

        for (std::size_t trace = 0; trace < snapshot.traces.size(); ++trace) {
            auto& samples = snapshot.traces[trace];
            samples.reserve(count);

            for (std::size_t i = 0; i < count; ++i) {
                const std::size_t index =
                    (start + i) % Synthesizer::display_history_size;
                samples.push_back(synth->display_history_[trace][index]);
            }
        }

        snapshots.push_back(std::move(snapshot));
    };

    static SDL_Color ColorFor(std::size_t index) {
        static constexpr SDL_Color colors[] = {
            { 80, 210, 255, 255 }, { 255, 110, 100, 255 },
            { 130, 230, 120, 255 }, { 220, 140, 255, 255 },
            { 255, 205,  90, 255 }, {  90, 230, 205, 255 },
            { 255, 145, 205, 255 }, { 185, 190, 255, 255 }
        };

        constexpr std::size_t colorCount = sizeof(colors) / sizeof(colors[0]);
        return colors[index % colorCount];
    }

    static void DrawLegend(SDL_Renderer* renderer, SDL_FRect bounds,
                           float legendWidth,
                           const std::vector<TraceSet>& snapshots) {
        if (snapshots.empty())
            return;

        // Spread names vertically through the available legend area.
        const float rowHeight = bounds.h / static_cast<float>(snapshots.size());

        for (std::size_t i = 0; i < snapshots.size(); ++i) {
            const SDL_Color color = ColorFor(i);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

            const float rowCenterY =
                bounds.y + (static_cast<float>(i) + 0.5f) * rowHeight;

            // Colored swatch and matching instrument name.
            SDL_RenderLine(renderer,
                           bounds.x + 5.0f, rowCenterY,
                           bounds.x + 17.0f, rowCenterY);

            std::string label = snapshots[i].name;
            const std::size_t maxChars =
                legendWidth > 24.0f
                    ? static_cast<std::size_t>((legendWidth - 24.0f) / 8.0f)
                    : 1;

            if (label.size() > maxChars) {
                if (maxChars > 3) {
                    label.resize(maxChars - 3);
                    label += "...";
                } else {
                    label.resize(maxChars);
                }
            }

            SDL_RenderDebugText(renderer,
                                bounds.x + 22.0f,
                                rowCenterY - 4.0f,
                                label.c_str());
        }
    }

    static float SampleAt(const std::vector<float>& samples, float position) {
        const std::size_t last = samples.size() - 1;
        position = std::clamp(position, 0.0f, static_cast<float>(last));

        const auto indexA = static_cast<std::size_t>(position);
        const auto indexB = std::min(indexA + 1, last);
        const float fraction = position - static_cast<float>(indexA);

        return samples[indexA] +
               (samples[indexB] - samples[indexA]) * fraction;
    }

    static void DrawPanel(SDL_Renderer* renderer, SDL_FRect rect,
                          const char* title, std::size_t traceIndex,
                          float xMultiplier, float yMultiplier,
                          const std::vector<TraceSet>& snapshots) {
        SDL_SetRenderDrawColor(renderer, 20, 22, 28, 255);
        SDL_RenderFillRect(renderer, &rect);

        SDL_SetRenderDrawColor(renderer, 66, 70, 80, 255);
        SDL_RenderRect(renderer, &rect);

        const float plotTop = rect.y + 23.0f;
        const float plotBottom = rect.y + rect.h - 5.0f;
        const float plotHeight = plotBottom - plotTop;
        const float plotWidth = rect.w - 10.0f;

        SDL_SetRenderDrawColor(renderer, 55, 59, 68, 255);
        const float centerY = plotTop + plotHeight * 0.5f;
        SDL_RenderLine(renderer, rect.x + 1.0f, centerY,
                       rect.x + rect.w - 1.0f, centerY);

        SDL_SetRenderDrawColor(renderer, 220, 224, 232, 255);
        SDL_RenderDebugText(renderer, rect.x + 8.0f, rect.y + 6.0f, title);

        if (plotWidth < 2.0f || plotHeight < 2.0f)
            return;

        xMultiplier = std::max(xMultiplier, 0.01f);
        yMultiplier = std::max(yMultiplier, 0.0f);

        const float amplitude = plotHeight * 0.44f;
        const int pixelCount = std::max(2, static_cast<int>(plotWidth));

        for (std::size_t synthIndex = 0;
             synthIndex < snapshots.size(); ++synthIndex) {
            const auto& samples = snapshots[synthIndex].traces[traceIndex];
            if (samples.size() < 2)
                continue;

            const SDL_Color color = ColorFor(synthIndex);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

            const float last = static_cast<float>(samples.size() - 1);
            const float visibleSpan =
                std::min(last, std::max(1.0f, last / xMultiplier));
            const float firstSample = last - visibleSpan;

            for (int x = 1; x < pixelCount; ++x) {
                const float fractionA =
                    static_cast<float>(x - 1) / (pixelCount - 1);
                const float fractionB =
                    static_cast<float>(x) / (pixelCount - 1);

                const float sampleA =
                    SampleAt(samples, firstSample + fractionA * visibleSpan);
                const float sampleB =
                    SampleAt(samples, firstSample + fractionB * visibleSpan);

                const float yA = std::clamp(
                    centerY - sampleA * amplitude * yMultiplier,
                    plotTop, plotBottom);
                const float yB = std::clamp(
                    centerY - sampleB * amplitude * yMultiplier,
                    plotTop, plotBottom);

                SDL_RenderLine(renderer,
                               rect.x + 5.0f + static_cast<float>(x - 1), yA,
                               rect.x + 5.0f + static_cast<float>(x), yB);
            }
        }
    }
};
