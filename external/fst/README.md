# FST (vendored)

FST 0.177.0 by IOhannes m zmölnig (IEM), a clean-room reimplementation of
the old VST2 plugin interface, GPLv3 or later (see LICENSE). Upstream:
https://git.iem.at/zmoelnig/FST. Copied from Debian's fst
0.177.0-1 source package, with three changes so nothing writes past what
was allocated:
- `MidiKeyName`: `keyName` is sized (64, its documented length) instead of a
  flexible array, plus the two trailing ints plugins expect.
- `VstPinProperties`: the 48-byte reserved tail plugins fill in (without it,
  scanning Cymatics Origin overran the stack and Windows aborted).
- `VstSpeakerArrangement`: room for 8 speakers built in, as JUCE and plugins
  allocate it (without it, a stereo layout overflowed the heap).

Basement uses it only to host older .dll/.vst plugins (JUCE expects these
headers at `pluginterfaces/vst2.x/`). Basement is not a Steinberg product
and is not "VST Compatible" in Steinberg's trademark sense.
