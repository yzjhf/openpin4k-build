OpenPin4K -- VPX engine (first cabinet test build)
==================================================

This folder is an "External Application" for the AtGames Legends Pinball 4K.
It contains a self-built copy of the open-source Visual Pinball (VPX) engine
compiled for the cabinet's RK3588 chip, plus a small launcher.

WHAT IT SHOULD DO
  Launch it from the cabinet's "External Applications" menu (entry name: vpx).
  It tries to open a built-in example table on the playfield.

  NOTE (diagnostic build): it auto-closes after ~60 seconds even if it works --
  that is expected. Then unplug the USB and look for the log file below.

IF SOMETHING GOES WRONG (this is the important part)
  The launcher writes a log file next to this README every time it runs:

        vpx-log.txt

  If the engine fails to start, shows a black screen, or quits immediately,
  please copy "vpx-log.txt" off the USB and send it back. That log usually
  tells us exactly what to fix WITHOUT needing another trip to the cabinet.

FILES HERE
  vpx.elf            the launcher you actually start (sets things up, then runs VPX)
  VPinballX_BGFX     the real VPX engine binary
  exampleTable.vpx   the table it tries to load
  assets/ plugins/ scripts/   engine support files
  vpx-log.txt        created on each run -- send this back if there's a problem
