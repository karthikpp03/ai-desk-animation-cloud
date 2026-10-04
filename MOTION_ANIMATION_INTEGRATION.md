# Motion animation integration

Integrated into `firmware/AIDeskCompanion` using the existing `MotionManager` + `AnimationManager` path.

- `idiot` reference: 50 frames @ 100 ms, 5.0 s playback
- `stupid` reference: 20 frames @ 100 ms, 2.5 s playback
- `dizzyyy` reference: 20 frames @ 50 ms, 3.0 s playback
- Reference frames are byte-RLE compressed and stored in `MotionAnimations.h`.
- Existing tilt direction reactions are unchanged.
- Pickup detection thresholds and sequence are unchanged; only the pickup reaction target changed to the supplied Idiot bitmap animation.
- Return-to-neutral is edge-triggered via `tiltOccurred`, so Stupid cannot restart while stationary.
- Fast rotation / high-energy shake uses the existing dizzy detection path.
