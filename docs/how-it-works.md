# How vkmAudio works

What the device does with the voices it is handed, and how the acoustics measure a space. The API is
in the headers; this page is what they cannot say.

## The device

`Device` is the mixer's surface onto miniaudio, and `decodeFile` the decoder's: only `device.cpp` and
`decode.cpp` include its header.
A voice reads its clip's samples in place and shares their ownership (`ClipSamples`), so a clip its
owner frees plays on to its end. A voice runs through its own low-pass to the output, or through the
room's reverb node when it rings in the room; the reverb's decay and level cross to the mixer thread
as atomics (`setRoomReverb`).

### Which thread may call it

All of `Device`, `render` included, is **main-thread only** and unguarded: the caller's frame calls
it, and the mixer thread is miniaudio's.

### The known races are miniaudio's

ThreadSanitizer reports races inside miniaudio on single aligned scalars (a voice's volume written
as the mixer reads it, a low-pass retuned as it runs). They are left alone: fixing them means forking
miniaudio, and no state of the library's own races.

### The voice budget

`MAX_ACTIVE_VOICES` caps live voices. At it, a new sound takes the place of a voice it outranks, one
of a lower `priority` or of its own and quieter as heard, or is refused (`Device::play`); a voice a
pause holds is kept. So music raised above the rest is never cut, and a gunshot takes a distant
footstep's place. It logs once per saturation, and it does not prevent clipping: gain staging is the
caller's. `MAX_HELD_VOICES` caps the voices held at all, those ramping out included, which leave
only as the mixer runs.

Every stop ramps the voice out over a few milliseconds so it does not click; its id reads as gone at
once, and `reapFinishedVoices` frees it after. Ids are never reused.

### Distance

A positioned voice fades from full volume at `minDistance` to silence at `maxDistance` by its
`rolloff`. `Linear` is miniaudio's own curve; `Inverse` is the library's (`inverseRolloffGain`),
applied as a gain over a flat curve, since miniaudio's inverse model never reaches silence, and
redone as the ear moves. With no listener active, a positioned voice is silent and a flat one plays
on.

### No device is a normal state

With no audio, `open` answers false, every other call does nothing, and the caller runs **silent,
not broken**: every sound reads as finished at once. To reach that path on a machine with sound:

```bash
PULSE_SERVER=/nonexistent ALSA_CONFIG_PATH=/nonexistent <program>
```

A device lost mid-run (a headset unplugged) is reopened by `reapFinishedVoices`, tried again every
two seconds, and what was playing carries on. A one-shot asked for while it is lost is not played:
its moment has passed.

`openOffline(rate, channels)` runs the same graph into a caller's buffer, pumped by `render`.

### The cursor is the device's

A voice's position in its clip is read from the mixer (`voiceCursor`), counting a seek asked for but
not yet applied. A caller keeps no copy: the mixer moves it between frames, so a copy would drop a
scrub or re-seek the mixer to a stale place.

## The acoustics

`Acoustics::` estimates what the world does to a sound from rays over a cast the caller supplies, a
`bool(origin, direction, reach, Strike&)` answering the nearest surface. It casts nothing itself and
keeps nothing: the caller decides when to listen and holds the rays.

**Occlusion.** `castOcclusionRays` casts from the ear to a sound's middle and to four points around
it, either side and above and below across the line of sight, so a doorway lets part of a sound
through. A side point the sound cannot reach is pulled back halfway to what stops it, and every ray
stops `SOURCE_CLEARANCE` short of its point, so a sound pressed against a surface is not blocked by
it. The share stopped (`blockedShare`) is the occlusion: its gain (`occludedGain`) and a low-pass
falling on a logarithmic scale (`occludedCutoff`), eased by `approach` so a door opening is heard
opening.

**The room.** Rays leave the ear along `roomDirection`'s even lattice, a few at a time, and bounce
`ROOM_BOUNCES` times (`traceRoomRay`). The share of their legs that struck is how shut in the space
is, and the struck legs' mean length how big (`estimateRoom`); Eyring's reverberation time over that
mean free path sets the decay (`reverbFor`). Open ground has none.

**One tank.** `ReverbTank` is Schroeder's design as Freeverb tunes it: damped combs in parallel, then
all-passes in series, each output channel's lines a little longer than the last so the tail is wide.
Every voice that rings feeds the one tank, tuned to the room the ear is in.
