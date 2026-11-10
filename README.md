# Synthesizer

A small Synthesizer/Midiplayer written partially by AI
(I'm sorry, I just didn't have the time to do it properly, but I assure you it wasn't vibe-coded).

I will rewrite it later to replace all the AI code, as I am planning to use it in [Total Domination](https://github.com/IamLegende7/total_domination).

## Compiling & Running

This Programm currently only runs on GNU/Linux (maybe MacOS, idk). To compile it you don't really need much, just CMake and it's dependencies should suffice, as Synthesizer comes with it's own copy of SDL3.

```bash
git clone --recurse https://github.com/IamLegende7/synthesizer
```

```bash
cd synthesizer
cmake -B build
cmake --build build
```

To run it, use:

```bash
./bin/synth <file>
```

## Keybinds:

Currently you sadly can't skip forward (or backwards) through the piece.

- \[Space\]: Pause/Play
- \[C\]: Play C4 on the standalone Synthesizer (using `instruments/Standard.json`) (is disabled when the piece is paused)

## Synthesizer settings

The settings of each instrument can be found in `instruments/`, so that you can change them to fit your prefrences.

I didn't have the motivation for a proper UI for fine-tuning the Synths.
