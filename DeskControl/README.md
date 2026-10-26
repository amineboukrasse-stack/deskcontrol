# DeskControl 2.1

Control your whole Windows PC from the couch with an Xbox-style controller: sticks drive the mouse and scroll wheel, buttons become keys, clicks, shortcuts and more. It works with every program, no game support needed.

DeskControl is the new name of this project, a ground-up modernisation of [Gopher360](https://github.com/Tylemagne/Gopher360) by Tylemagne (GPL v3). The original console app is kept in `legacy/`.

## What's new

| | Gopher360 1.x | DeskControl |
|---|---|---|
| Interface | black console window, edit hex codes in `config.ini` | Modern GUI with a **live controller view**; click a button, pick its action |
| Key binding | look up virtual-key codes | **Capture key** button, searchable key list, **Ctrl/Shift/Alt/Win shortcuts**, mouse X1/X2, media keys |
| Profiles | one file | Any number of **profiles**, plus a **shift layer** (hold a button for a second set of bindings) |
| Combos | fixed list | Add your own combos; **"Set from controller"** records them by pressing the buttons |
| Per-app behaviour | none | **App rules**: pause DeskControl or switch profile when a program is in front |
| Cursor | speed presets | Editable presets, response curve, **precision mode**, relative-mouse mode for games, live dead-zone tuning |
| Running | console window | **Tray icon**, start with Windows, start minimised, single instance |
| Typing | toggle the Windows keyboard with a combo | **Built-in on-screen keyboard** that opens by itself when you click a text box, never steals focus, and is driven by the cursor or the D-pad (see below) |
| Controller | first pad only | Pick the controller, hot-plug detection, battery level |
| Runtime | needs the VC++ redistributable | **Single static .exe**, nothing to install |

Bugs fixed along the way: arrow keys were sent as *numpad* arrows (broken with NumLock off); the weak scroll axis could scroll the wrong way; the loop froze for 450 ms on every rumble pulse; one bad `config.ini` line called `exit()`; the "high" speed fallback was 10x too small; `Start` typed the Windows key even when you were pressing Start+Back.

## On-screen keyboard

DeskControl has its own full keyboard (letters, numbers, F-keys, arrows, Ins/Del/Home/End/PgUp/PgDn, Ctrl/Win/Alt/Shift).

- **It never takes focus.** Whatever you were typing into keeps receiving the keys, so there is no clicking back into the text box.
- **Opens by itself** when you click a text box, or any time with the *On-screen keyboard* action (bind it to a button, or use the tray menu / Settings). Drag the top bar to move it, drag an edge to resize it, and the cross closes it.
- **Type with the cursor:** move the cursor over a key and press `A` (hold for key repeat).
- **Type with the D-pad:** while the cursor is on the keyboard the D-pad jumps from key to key (the arrow keys are on the keyboard itself). Turn this off under Settings if you prefer.
- **Shift / Ctrl / Alt are sticky:** tap one, then tap the next key (Shift+A, Ctrl+C...). Caps Lock and the Win key are single taps.
- Key captions follow the keyboard layout of the program you are typing into.
- Prefer the Windows keyboard? Settings -> On-screen keyboard -> *Use the DeskControl keyboard* off.

## Using it

Run `DeskControl.exe`. Left stick = mouse, right stick = scroll, `A`/`X` = left/right click. Everything is configurable in the window; changes apply instantly and are saved automatically.

Default combos: **Start + Back** pause/resume - **LB + RB** cycle cursor speed - **Back + LB** toggle rumble.

Notes
- **Elevated programs** (Task Manager, installers, some games) ignore injected input unless DeskControl also runs as administrator - use *Restart as administrator* on the Dashboard.
- **PlayStation / Switch pads** work through DS4Windows, Steam Input or similar, which present them as Xbox controllers.
- Buttons that belong to a combo wait ~120 ms before acting alone (adjustable under *Combos*). By default only Start, Back, LB and RB are affected - not the D-pad.
- **Importing Gopher360 1.x settings:** *Profiles & Apps -> Import Gopher 1.x config.ini*.
- **Portable mode:** put an empty `portable.txt` next to the exe; data then lives in `DeskControlData\`. Otherwise it is in `%APPDATA%\DeskControl` (settings from an older `%APPDATA%\Gopher360` folder are moved there automatically). Profiles are plain text (`profiles\*.ini`) and safe to edit by hand.

## Building

Requires CMake 3.16+ and a C++17 compiler. Dear ImGui is vendored in `third_party/`.

```
build.bat                       :: Visual Studio 2022 (run from a developer prompt)
```
or manually:
```
cmake -S . -B build -A x64
cmake --build build --config Release        :: -> build\Release\DeskControl.exe
```
MinGW-w64 (also works as a Linux cross-compiler) is supported with the same CMake files. A GitHub Actions workflow in `.github/workflows` builds the exe on every push.

### Tests
The input logic (`model`, `core`, `store`, keyboard layout) has no Windows dependency and is covered by 130+ checks - chords, shift layer, key repeat, dead zones, scroll direction, precision mode, file formats and the 1.x importer:
```
cmake -S . -B build-tests -DDESKCONTROL_BUILD_TESTS=ON && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
```

## Layout
```
src/model.*     profiles, bindings, text format, 1.x importer   (portable)
src/core.*      input state machine: chords, layers, sticks     (portable, tested)
src/store.*     profile / settings files                        (portable, tested)
src/kbdlayout.* on-screen keyboard layout + D-pad navigation    (portable, tested)
src/keyboard.*  on-screen keyboard window                        (Windows)
src/engine.*    polling thread, XInput, SendInput, rumble       (Windows)
src/ui*.cpp     Dear ImGui interface        src/main.cpp   window, D3D11, tray
```

## Licence
GPL v3 (see `LICENSE`). Dear ImGui is MIT (`third_party/imgui/LICENSE_imgui.txt`).
