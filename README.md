# FX Locks 0.2 (Digitakt MKI, OS 1.54)

Parameter locks on the **delay** and **reverb**, made from the audio track itself: no MIDI track
setup.

| Page | Lockable knobs |
|---|---|
| Delay | **TIME** (note values only), **WID** (stereo width), **FDBK** (feedback) |
| Reverb | **DEC** (decay) |

## New in 0.2

- **Locks now play.** 0.1 stored the locks but the delay never changed: it waited for a signal the
  firmware doesn't give for these slots. 0.2 reads each step's FX locks itself as the trig fires.
- **TIME locks snap to note values.** Turning TIME with a trig held steps through 1/128, 1/64,
  1/64., 1/32, 1/32., 1/16, 1/16., 1/8, 1/8., 1/4, 1/4., 1/2, 1/2., 1/1 (the Digitakt's own delay
  note values, "." = dotted), so a locked delay always lands on the beat. WID, FDBK and DEC still
  move freely.

## How to use

1. On an **audio track**, go to the delay (or reverb) page.
2. **Hold a trig** (or several) and **turn** TIME / WID / FDBK / DEC, exactly like any other lock.
   The knob shows the step's locked value while you hold it.
3. Play: when that step plays, the shared delay/reverb jumps to the locked value. It keeps it until
   the **same track's next trig**; a trig without the lock puts back the pattern's own setting.

- Two tracks locking the same FX knob at the same moment: the **higher track number wins**.
- The locks are stored on the step like any lock: they save with the pattern, copy and paste with
  the trig, and are cleared the usual ways.
- Delay TIME changes glide slightly (the engine smooths it), like turning the knob by hand.

## How it works (plain version)

- Each step has 53 lock slots, one for each sound setting. Slots 48-51 are used by no audio
  machine, so the FX locks live there.
- When you turn one of these 4 knobs with a trig held, the mod writes the lock into that slot of
  the held step(s) with the firmware's own lock routine.
- When a trig fires, the mod reads that step's FX locks. About 1500 times a second it copies the
  active lock into the shared delay/reverb, or puts the pattern's value back when no track holds
  one.

## Compatibility

- Can be in one build with machine-pages (it hooks a different spot in the render). But a
  machine-pages machine that uses **extra values** keeps them in the same slots 46-52, so on such a
  track don't use FX locks (or Deja Vu's RND destinations).
- Audio tracks only. Needs **seq-events**.

## Install

Load `fxlocks-0.2.elemod` in Elekloader together with **core** and **seq-events**, build your OS
and flash it as usual. No `.syx` is shared here (it would contain Elektron firmware).

Licence: GPL-2.0-or-later (see LICENSE).
