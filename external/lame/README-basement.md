# LAME 3.100 (MP3 encoder)

Basement's MP3 export uses LAME's encoder library, libmp3lame, from
https://lame.sourceforge.io (lame-3.100.tar.gz, SHA-256
ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e).

LAME is licensed under the LGPL (see COPYING and LICENSE here). Basement is
open source (AGPLv3), so anyone can rebuild it against a different or
modified LAME, which is what the LGPL asks of a program that links it.

What's here:

- `libmp3lame/`: the library's C sources and headers, unchanged (from
  `vector/`, only the header; the SSE code isn't built).
- `include/lame.h`: the public header, unchanged.
- `config.h`: ours, in place of the one LAME's `./configure` would write.
  It turns off the assembly and SSE code paths so one build works on
  Windows and on both kinds of Mac.

Not included: the `lame` command-line frontend, the mpglib decoder, and
LAME's Windows DLL, ACM and DirectShow wrappers.

Thanks to the LAME project, www.mp3dev.org.
