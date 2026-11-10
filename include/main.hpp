#ifndef MAIN_HPP
#define MAIN_HPP

#include <SDL3/SDL.h>
#include "stream.hpp"
#include "synth.hpp"
#include "midi_player.hpp"
#include "synth_display.hpp"

struct AppContext {
    SDL_Renderer* renderer = nullptr;
    SDL_Window* window = nullptr;
    SDL_Texture* cache;
    AudioStream stream;
    MidiPlayer midi;
    SynthDisplay display;
    Synthesizer synthesizer;
    int window_width, window_height;
};

#endif