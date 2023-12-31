# CrazyTown
![CrazyTown](https://github.com/matiasjpedro/CrazyTown/assets/7761322/2d63ce4c-88d0-4cf2-ae34-d1fb0a2eef5a)

#### by Matias Pedro

A portable application to easily filter logs, written in C/C++.

[Binaries](https://github.com/matiasjpedro/CrazyTown/releases)

## Motivation

As a gameplay programmer I found myself spending a lot of hours looking at logs provided by QA to track down the root cause
of specific issues. I tried a couple of Log filter applications in the past and when the filter started to become a little bit
complex or if the file was big those ended up becoming unresponsive or ultra slow. So for the sake of fun and learning I decide
to code my own, using and learning all the techniques that I could apply to make it blazingly fast. I combined that speed with a
feature set that I always thought it will be useful to have in an application like this.

## Used Libraries:

[cJSON](https://github.com/DaveGamble/cJSON) for saving settings
[ImGUI](https://github.com/ocornut/imgui) for ui handling

## Features:

* Portable, .exe + .dll = 4 MB.
* Filter using C synthax instead of complex regex.
* Drag and drop files.
* Copy/Paste text To/From Clipboard
* Save/Delete/Override Filter presets.
* Copy/Paste filter preset To/From Clipboard. (Useful to share filters with colleagues)
* Filter highlight, different colors per filter.
* Toggle each individual filter (Cherrypick).
* Peeking functionality (Ctrl+Click) in the filtered line it will show you that line the the full view of the file.
* Word Selection.
* Line Selection.
* Multithread filter parsing with almost perfect scalability until we hit diminish return.
* AVX instructions for filter parsing (15/10x speeds than a linear haystack search).
* Stream latest file from "x" folder. (Useful to get the output of whatever program that writes in folder like Unreal)
* Open source.

## Usage: TODO

I'm gonna record a video showcasing the features and the usage of it.

KEYBINDS when hovering the output view:

* [F5]                 Will refresh the loaded file. If new content is available it will append it.
* [Ctrl+C]             Will copy the content of the output to the clipboard.
* [Ctrl+V]             Will paste the clipboard into the output view. 
* [Ctrl+MouseWheel]    Will scale the font. 
* [Ctrl+Click]         Will peek that filtered hovered line in the full view of the logs.
* [MouseButtonBack]    Will go back from peeking into the filtered view.
* [Alt]                Will enter in word selection mode when hovering a word. 
* [Shift]              Will enter in line selection mode when hovering a line. 
* [MouseWheel]         While in word/line selection mode it will expand/shrink the selection.
* [MouseMiddleClick]   While in word/line selection mode it will copy the selection to the clipboard.
* [MouseRightClick]    Will open the context menu with some options.

## Building:

Currently I'm building it with MSVC, just run the misc/build.bat, and it will spit out the new binary in the build folder.

## Develop tools used:

[10xEditor](https://10xeditor.com/) for source edit
[RemedyBG](https://remedybg.itch.io/remedybg) for debugging

## Special Thanks:

Medo Osman, Diego Sugue, Hernan Stescovich, Marcus Frandsen for test the app and provide unvaluable feedback.

Nicolas Maier for helping refining the SIMD instruccions to extract up to the last drop of performance.

[HandmadeNetwork](https://handmade.network/) Special shotout to the discord, a lot of crazy talented people there that are always open to help with technical questions.
