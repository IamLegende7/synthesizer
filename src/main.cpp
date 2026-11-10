#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cmath>

#include "main.hpp"
#include "MidiFile.h"

constexpr float SCALING = 0.5;
constexpr int TARGET_FRAME_RATE = 30;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    AppContext* context = new AppContext{};
    if (!SDL_CreateWindowAndRenderer("Synth", 800, 600, SDL_WINDOW_RESIZABLE, &context->window, &context->renderer)) {
        SDL_Log("Couldn't create window and renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (!SDL_SetRenderVSync(context->renderer, 1)) {
        SDL_Log("VSync could not be enabled: %s", SDL_GetError());
    }
    SDL_srand(0);

    if (!context->stream.Init()) {
        SDL_Log("Could not init audio stream!");
        return SDL_APP_FAILURE;
    }

    context->stream.AddPlayer(&context->synthesizer);
    context->synthesizer.LoadJson("instruments/Standard.json");
    context->midi.Init(&context->stream);
    context->midi.SetSettingsDir(std::filesystem::path("instruments"));
    context->display.Init(context->renderer, &context->midi, nullptr);
    context->stream.SetPlayback(true);

    if (argc > 1) {
        std::string error;
        if (context->midi.Load(std::filesystem::path(argv[1]))) {
            SDL_SetRenderDrawColor(context->renderer, 26, 26, 26, 255);
            SDL_RenderClear(context->renderer);
            int outputWidth, outputHeight;
            SDL_GetRenderOutputSize(context->renderer, &outputWidth, &outputHeight);
            context->cache = SDL_CreateTexture(
                context->renderer,
                SDL_PIXELFORMAT_RGBA8888,
                SDL_TEXTUREACCESS_TARGET,
                outputWidth,
                outputHeight
            );
            SDL_SetTextureScaleMode(context->cache, SDL_SCALEMODE_NEAREST);
            context->display.ReDraw(SDL_FRect{0.0f, 0.0f, outputWidth*SCALING, outputHeight*SCALING});

            context->stream.SetPlayback(true);
            SDL_Log("Playing MIDI file: %s", argv[1]);
        } else {
            SDL_Log("Could not load MIDI file!");
        }
    }

    *appstate = context;

    SDL_Log("Init done!");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    AppContext* context = (AppContext*)appstate;
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    } else if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        if ((event->window.data1*SCALING != context->window_width) || (event->window.data2*SCALING != context->window_height)) {
            context->window_width  = std::floor(event->window.data1*SCALING);
            context->window_height = std::floor(event->window.data2*SCALING);
            SDL_DestroyTexture(context->cache);
            context->cache = SDL_CreateTexture(
                context->renderer,
                SDL_PIXELFORMAT_RGBA8888,
                SDL_TEXTUREACCESS_TARGET,
                context->window_width,
                context->window_height
            );
            SDL_SetTextureScaleMode(context->cache, SDL_SCALEMODE_NEAREST);
            SDL_SetRenderTarget(context->renderer, context->cache);
            SDL_SetRenderDrawColor(context->renderer, 26, 26, 26, 255);
            SDL_RenderClear(context->renderer);
            SDL_SetRenderViewport(context->renderer, NULL);
            SDL_SetRenderLogicalPresentation(context->renderer, context->window_width, context->window_height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
            context->display.ReDraw(SDL_FRect{0.0f, 0.0f, static_cast<float>(context->window_width), static_cast<float>(context->window_height)});
            SDL_Log("New window size: %d, %d", context->window_width, context->window_height);
        }
    } else if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
        if (event->key.key == SDLK_SPACE) {
            context->stream.TogglePlayback();
        } else if (event->key.key == SDLK_C) {
            context->synthesizer.NoteOn(60, 100);
        }
    }

    if (event->type == SDL_EVENT_KEY_UP) {
        if (event->key.key == SDLK_C) {
            context->synthesizer.NoteOff(60);
        }
    }

    /*if (event->type == SDL_EVENT_DROP_FILE) {
        std::string error;

        if (context->midi_player.LoadMidi(event->drop.data)) {
            context->midi_player.Play();
            context->stream.SetPlayback(true);
            SDL_Log("Playing MIDI file: %s", event->drop.data);
        } else {
            SDL_Log("Could not load MIDI file: %s", error.c_str());
        }
    }*/

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    AppContext* context = (AppContext*)appstate;

    static Uint32 last_time = SDL_GetTicks();
    static int ms_per_frame = 1000 / TARGET_FRAME_RATE;

    Uint32 current_time = SDL_GetTicks();
    int delta_time = current_time - last_time;
    last_time = current_time;


    SDL_SetRenderTarget(context->renderer, context->cache);
    context->display.Draw(SDL_FRect{0.0f, 0.0f, static_cast<float>(context->window_width), static_cast<float>(context->window_height)});

    SDL_SetRenderTarget(context->renderer, nullptr);
    SDL_RenderTexture(context->renderer, context->cache, nullptr, nullptr);
    SDL_RenderPresent(context->renderer);


    int elapsed_time = SDL_GetTicks() - current_time;
    if (elapsed_time < ms_per_frame) {
        SDL_Delay(ms_per_frame - elapsed_time);
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    SDL_Log("Exiting..");

    AppContext* context = static_cast<AppContext*>(appstate);

    if (context) {
        if (context->renderer)
            SDL_DestroyRenderer(context->renderer);
        if (context->window)
            SDL_DestroyWindow(context->window);

        delete context;
    }

    SDL_Quit();
    SDL_Log("Done!");
}