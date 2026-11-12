# vkmAudio

A 3D mixer over miniaudio: a playback device, or a mix into a buffer; voices ranked by priority;
linear or inverse distance rolloff; occlusion heard quieter and duller; one room's reverb; decoded
clips whose samples the voices share; a decoder for wav, mp3 and flac; and the acoustics' estimates
over a ray cast the caller supplies. It knows no engine - no entities, no assets, no physics - and
keeps nothing but the voices it plays.

## Conventions

- Y is up and lengths are meters; the listener's up is passed in.
- Every gain is linear: 0.5 is half the amplitude. A negative or non-finite one is silence.
- A positioned voice wants a mono clip: each channel goes to the output channel it was written for,
  so a stereo clip only leans.
- `Device` is the main thread's alone, `render` included; the mixer's thread is miniaudio's.
- No device is a normal state: `open` answers false, every other call does nothing, and the caller
  runs silent.

## A mix

```cpp
Vkm::Audio::Device device;
device.open();                                   // or openOffline(48000, 2), pumped by render()
Vkm::Audio::Clip clip;
Vkm::Audio::decodeFile("door.wav", clip);
Vkm::Audio::VoiceParams params;
params.position = {2.0f, 0.0f, -3.0f};
const Vkm::Audio::VoiceId voice = device.play(clip, params);
device.setListener(ear, forward, up);            // each frame, then updateVoice and reapFinishedVoices
```

`docs/how-it-works.md` says what the device does with them, and why.

## Requirements

CMake 3.25 or above and a C++17 compiler.

## Use

Add it as a submodule, with its own submodule (miniaudio), and `add_subdirectory` it after the two
targets it requires and does not vendor: `glm`, which crosses its API, and `vkm_log`. The build stops
with the reason when either is missing. It adds `miniaudio` from `modules/miniaudio` unless the
consumer already has the target.

```
git submodule add https://github.com/V-KMilev/vkmAudio.git modules/vkmAudio
git submodule update --init modules/vkmAudio/modules/miniaudio
```

The library is `vkm_audio`, static and position-independent, for linking into a library of the
consumer's own. Headers are included by the library's folder: `#include "vkm_audio/device.h"`;
everything is in `Vkm::Audio`. miniaudio stays behind it: no header of the library includes miniaudio's.

| Option              | Default | What                                                                   |
|---------------------|---------|------------------------------------------------------------------------|
| `VKM_AUDIO_ASSERTS` | `OFF`   | The library's `VKM_ASSERT`s run whatever the build type (`-UNDEBUG` on its own sources) |

## What it holds

| Header          | What                                                                                   |
|-----------------|----------------------------------------------------------------------------------------|
| `device.h`      | `Device`, `VoiceParams`, `VoiceId`, `Rolloff`, the voice limits, `inverseRolloffGain`   |
| `clip.h`        | `Clip`, `ClipSamples`, `findClipFault`                                                  |
| `decode.h`      | `decodeFile`, `DECODABLE_EXTENSIONS`                                                    |
| `acoustics.h`   | `Acoustics::`: occlusion and the room's estimate, over any ray cast                     |
| `reverb_tank.h` | `ReverbTank`, the room's reverberation                                                  |

Each is under `src/vkm_audio/`.

## In vkmEngine

The engine pins it at `modules/vkmAudio` and links it whole into `libvkm_core`. A change is a commit
here, then an engine commit that moves the pin.
