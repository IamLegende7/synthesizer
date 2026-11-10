#ifndef MIDI_PLAYER_HPP
#define MIDI_PLAYER_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "synth.hpp"

class MidiPlayer : public AudioPlayer {
    friend class SynthDisplay;
public:
    ~MidiPlayer();

    bool Init(AudioStream* stream);
    void SetSettingsDir(std::filesystem::path path);
    bool Load(std::filesystem::path path);

    // Load() starts playback. These are provided for replay and stopping.
    void Play();
    void Stop();
    void TogglePlayback();

    void Callback(int sample_count, AudioBuffer* output) override;

private:
    enum class EventType { NoteOn, NoteOff };

    struct Event {
        Sint64 frame = 0;
        std::size_t track = 0;
        EventType type = EventType::NoteOn;
        int note = 0;
        int velocity = 0;
        std::size_t order = 0;
    };

    struct Track {
        std::unique_ptr<Synthesizer> synth;
        std::string instrument;
    };

    AudioStream* stream_ = nullptr;
    std::filesystem::path settings_dir_;
    std::vector<Track> tracks_;
    std::vector<Event> events_;

    std::size_t next_event_ = 0;
    Sint64 position_ = 0;
    Sint64 end_frame_ = 0;
    bool playing_ = false;
    bool end_notes_sent_ = false;

    void RenderSegment(int sample_count, int output_offset, AudioBuffer* output);
    void ProcessEvent(const Event& event);
};

#endif
