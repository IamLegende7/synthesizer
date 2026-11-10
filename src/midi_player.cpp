#include "midi_player.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "MidiFile.h"

namespace {

// Standard General MIDI program names, numbered 0–127.
const std::array<const char*, 128> gm_instruments = {
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano",
    "Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2", "Harpsichord",
    "Clavinet", "Celesta", "Glockenspiel", "Music Box", "Vibraphone",
    "Marimba", "Xylophone", "Tubular Bells", "Dulcimer", "Drawbar Organ",
    "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion",
    "Harmonica", "Tango Accordion", "Acoustic Guitar (nylon)",
    "Acoustic Guitar (steel)", "Electric Guitar (jazz)",
    "Electric Guitar (clean)", "Electric Guitar (muted)", "Overdriven Guitar",
    "Distortion Guitar", "Guitar harmonics", "Acoustic Bass", "Electric Bass (finger)",
    "Electric Bass (pick)", "Fretless Bass", "Slap Bass 1", "Slap Bass 2",
    "Synth Bass 1", "Synth Bass 2", "Violin", "Viola", "Cello", "Contrabass",
    "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
    "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2",
    "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit", "Trumpet",
    "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section",
    "Synth Brass 1", "Synth Brass 2", "Soprano Sax", "Alto Sax", "Tenor Sax",
    "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet", "Piccolo",
    "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle",
    "Ocarina", "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)",
    "Lead 4 (chiff)", "Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)",
    "Lead 8 (bass + lead)", "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)",
    "Pad 4 (choir)", "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)",
    "Pad 8 (sweep)", "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)",
    "FX 4 (atmosphere)", "FX 5 (brightness)", "FX 6 (goblins)",
    "FX 7 (echoes)", "FX 8 (sci-fi)", "Sitar", "Banjo", "Shamisen", "Koto",
    "Kalimba", "Bag pipe", "Fiddle", "Shanai", "Tinkle Bell", "Agogo",
    "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum",
    "Reverse Cymbal", "Guitar Fret Noise", "Breath Noise", "Seashore",
    "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot"
};

} // namespace

MidiPlayer::~MidiPlayer() {
    if (stream_) {
        stream_->RemovePlayer(this);
        stream_ = nullptr;
        parent = nullptr;
    }
}

bool MidiPlayer::Init(AudioStream* stream) {
    if (!stream) {
        return false;
    }

    if (stream_ == stream) {
        return true;
    }

    if (stream_) {
        stream_->RemovePlayer(this);
    }

    stream_ = stream;
    parent = stream_;

    {
        AudioStreamGuard guard(stream_);
        stream_->AddPlayer(this);
    }

    return true;
}

void MidiPlayer::SetSettingsDir(std::filesystem::path path) {
    settings_dir_ = std::move(path);
}

bool MidiPlayer::Load(std::filesystem::path path) {
    smf::MidiFile midi;
    if (!midi.read(path.string())) {
        return false;
    }

    midi.doTimeAnalysis();

    // If Load() is called while the stream is active, protect the player state.
    AudioStreamGuard guard(stream_);

    tracks_.clear();
    events_.clear();
    next_event_ = 0;
    position_ = 0;
    end_frame_ = 0;
    end_notes_sent_ = false;
    playing_ = false;

    const float rate = stream_ ? stream_->sample_rate : 48000.0f;
    const int track_count = midi.getTrackCount();

    tracks_.resize(static_cast<std::size_t>(track_count));

    for (int t = 0; t < track_count; ++t) {
        int program = 0;
        bool found_program = false;
        std::string instrument_name;

        for (int i = 0; i < midi[t].size(); ++i) {
            const auto& message = midi[t][i];

            if (instrument_name.empty() && message.isTrackName()) {
                instrument_name = message.getMetaContent();
            }

            if (!found_program && message.isPatchChange()) {
                program = std::clamp(message.getP1(), 0, 127);
                found_program = true;
            }
        }

        if (instrument_name.empty()) {
            instrument_name = gm_instruments[static_cast<std::size_t>(program)];
        }

        Track& track = tracks_[static_cast<std::size_t>(t)];
        track.instrument = instrument_name;
        track.synth = std::make_unique<Synthesizer>();

        const std::filesystem::path instrument_file = settings_dir_ / (track.instrument + ".json");

        const std::filesystem::path settings_file =
            std::filesystem::exists(instrument_file)
                ? instrument_file
                : settings_dir_ / "Standard.json";

        if (std::filesystem::exists(settings_file)) {
            SDL_Log("Loading synth preset: \"%s\"", settings_file.string().c_str());
            track.synth->LoadJson(settings_file.string());
        }

        for (int i = 0; i < midi[t].size(); ++i) {
            const auto& message = midi[t][i];

            if (!message.isNoteOn() && !message.isNoteOff()) {
                continue;
            }

            const double seconds = std::max(0.0, message.seconds);
            const Sint64 frame = static_cast<Sint64>(
                std::llround(seconds * static_cast<double>(rate)));

            Event event;
            event.frame = frame;
            event.track = static_cast<std::size_t>(t);
            event.note = message.getKeyNumber();
            event.order = events_.size();

            if (message.isNoteOn() && message.getVelocity() > 0) {
                event.type = EventType::NoteOn;
                event.velocity = message.getVelocity();
            } else {
                event.type = EventType::NoteOff;
                event.velocity = 0;
            }

            events_.push_back(event);
            end_frame_ = std::max(end_frame_, frame);
        }
    }

    std::stable_sort(events_.begin(), events_.end(),
        [](const Event& a, const Event& b) {
            if (a.frame != b.frame) return a.frame < b.frame;
            return a.order < b.order;
        });

    playing_ = true;
    return true;
}

void MidiPlayer::Play() {
    next_event_ = 0;
    position_ = 0;
    end_notes_sent_ = false;
    playing_ = true;
}

void MidiPlayer::Stop() {
    AudioStreamGuard guard(stream_);

    playing_ = false;
    next_event_ = 0;
    position_ = 0;
    end_notes_sent_ = false;

    for (Track& track : tracks_) {
        if (track.synth) {
            track.synth->AllNotesOff();
        }
    }
}

void MidiPlayer::TogglePlayback() {
    if (playing_)
        Stop();
    else
        Play();
}

void MidiPlayer::ProcessEvent(const Event& event) {
    if (event.track >= tracks_.size() || !tracks_[event.track].synth) {
        return;
    }

    Synthesizer& synth = *tracks_[event.track].synth;
    if (event.type == EventType::NoteOn) {
        synth.NoteOn(event.note, event.velocity);
    } else {
        synth.NoteOff(event.note);
    }
}

void MidiPlayer::RenderSegment(
    int sample_count,
    int output_offset,
    AudioBuffer* output
) {
    if (sample_count <= 0) {
        return;
    }

    for (Track& track : tracks_) {
        if (!track.synth) {
            continue;
        }

        AudioBuffer buffer{};
        track.synth->Callback(sample_count, &buffer);

        for (int i = 0; i < sample_count; ++i) {
            (*output)[static_cast<std::size_t>(output_offset + i)] +=
                buffer[static_cast<std::size_t>(i)];
        }
    }
}

void MidiPlayer::Callback(int sample_count, AudioBuffer* output) {
    if (!output) {
        return;
    }

    output->fill(0.0f);

    // AudioStream's documented buffer size is at most buffer_sample_count.
    sample_count = std::clamp(
        sample_count, 0, static_cast<int>(buffer_sample_count));

    int rendered = 0;

    while (rendered < sample_count) {
        const Sint64 block_end = position_ + (sample_count - rendered);

        if (playing_ && next_event_ < events_.size() &&
            events_[next_event_].frame <= block_end) {
            const Sint64 event_frame = std::max(position_, events_[next_event_].frame);
            const int segment = static_cast<int>(event_frame - position_);

            RenderSegment(segment, rendered, output);
            rendered += segment;
            position_ += segment;

            // Apply every event at this frame before rendering the next sample.
            while (next_event_ < events_.size() &&
                   events_[next_event_].frame <= position_) {
                ProcessEvent(events_[next_event_]);
                ++next_event_;
            }

            continue;
        }

        const int segment = sample_count - rendered;
        RenderSegment(segment, rendered, output);
        rendered += segment;
        position_ += segment;
    }

    if (playing_ && next_event_ >= events_.size() &&
        position_ >= end_frame_ && !end_notes_sent_) {
        for (Track& track : tracks_) {
            if (track.synth) {
                track.synth->AllNotesOff();
            }
        }
        end_notes_sent_ = true;
        playing_ = false;
    }

    for (float& sample : *output) {
        sample *= volume;
    }
}
