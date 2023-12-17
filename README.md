# CrazyTown
![CrazyTownIMG](https://github.com/matiasjpedro/CrazyTown/assets/7761322/ae122f0e-a2a2-4916-9777-3c6ae77965ff)

#### by Matias Pedro

A portable and fast application to filter logs, written in C/C++.

#### Motivation

As a gameplay programmer I found myself putting a lot of hours looking at logs provided by QA to track down the root cause
of specific issues. I tried a couple of Log filter applications in the past and when the filter started to become a little bit
complex or if the file was big those ended up becoming unresponsible or ultra slow. So for the sake of fun and learning I decide
to code my own, using and learning all the techniques that I could apply to make it blazingly fast. I combined that speed with a
feature set that I always thought it will be useful to have in an application like this.

#### Used Libraries:

[cJSON](https://github.com/DaveGamble/cJSON) for saving settings
[ImGUI](https://github.com/ocornut/imgui) for ui handling

## Features:

* Filter using C synthax instead of complex regex.
* Drag and drop files.
* Copy/Paste text To/From Clipboard
* Stream latest file from "x" folder.
* Save/Delete/Override Filter presets.
* Copy/Paste filter preset To/From Clipboard.
* Word highlight.
* Toggle each individual filter (Cherrypick)
* Peeking functionality (Ctrl+Click) in the filtered line it will show you that line the the full view of the file.
* Word Selection (Alt to enable word selection, scroll wheel to increase/decrease selection, middle click to copy to the clipboard).
* Line Selection (Shift to enable word selection, scroll wheel to increase/decrease selection, middle click to copy to the clipboard).
* Multithread filter parsing with almost perfect scalability until we hit diminish return.
* AVX instructions for filter parsing (15/10x speeds than a linear haystack search).
* Open source

## Usage: TODO

I'm gonna record a video showcasing the features and the usage of it.

## Building:

Currently I'm building it with MSVC, just run the misc/build.bat, and it will spit out the new binary in the build folder.

## Develop tools used:

[10xEditor](https://10xeditor.com/) source edit
[RemedyBG](https://remedybg.itch.io/remedybg) for debugging

## Special Thanks:

Medo Osman, Diego Sugue, Hernan Stescovich, Marcus Frandsen for test the app and provide unvaluable feedback.
Nicolas Maier for helping refining the SIMD instruccions to extract up to the last drop of performance.
