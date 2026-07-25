# Loose shaders of the port

Copies of the files the port overrides in `Dead Air/gamedata/shaders/`. The game reads them from
there, not from here — this directory exists so the shaders are under version control at all.

Everything the mod ships lives inside its archives, and our edits are loose files placed on top. They
are therefore outside any repository, and losing one means carving it out of the archive again and
redoing the work. That happened twice in a single evening while getting motion vectors working.

To restore after an accident, or to see what a file looked like before an edit:

    cp da_gamedata/shaders/r3/<file> "../Dead Air/gamedata/shaders/r3/"

The authoritative source of the *original* files is not the archive but a dump taken through the
engine's own file system (console command `da_dump_shaders r3`, output in
`appdata/logs/vfs_shaders/`) — offline unpacking of Dead Air's archives yields stale revisions.
