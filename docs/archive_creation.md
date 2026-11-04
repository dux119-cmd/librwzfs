# Archive creation and revision

## create_archive
`rwzfs::create_archive(source_dir, output, options)` builds an archive with mkdwarfs, using the CLI flags:

    --force --progress=simple --categorize --no-history --compression=<compression>

- Default compression: `zstd:level=12:wlog=27:strat=3`. The `wlog` and `strat` parameters need a dwarfs build whose zstd exposes them. Builds without that support reject the string (`tool_failed`, with mkdwarfs's message on stderr). Pass `options.compression` to override.
- Writes `<output>.tmp`, then renames it to `<output>`. A failed run removes the temp file, so no partial archive is left at `<output>`.
- The mkdwarfs executable is the one found at build time, else `mkdwarfs` on PATH. `options.mkdwarfs` overrides it.
- Implemented with posix_spawn on POSIX. On Windows it returns `unsupported_platform` for now.

Error values: `source_missing`, `tool_not_found`, `tool_failed`, `io`, `unsupported_platform`.

## revise_archive
`rwzfs::revise_archive(mountpoint, options)` revises the archive mounted at `mountpoint`:

1. Requires an archive mounted at exactly `mountpoint` with a non-empty overlay. Otherwise `no_changes` or `mount_not_found`.
2. Flattens the merged view (overlay wins; whiteouts, opaque directories and renames applied) into a scratch directory next to the archive. Files, directories, archive symlinks, modes and modification times are reproduced.
3. Runs `create_archive` on the scratch tree, writing `<stem>-revN<ext>` next to the archive.
4. Removes the scratch tree.
5. Unmounts (which releases the overlay lock) and clears the old overlay's `data/`, `whiteouts/` and `opaque/`.
6. Returns the new archive path. The caller mounts it to continue.

Naming: the stem is the archive file name up to its first dot, as in the overlay rule.
- `archive.dwarfs` → `archive-rev1.dwarfs`
- `archive-rev1.dwarfs` → `archive-rev2.dwarfs`
- `archive-revx.dwarfs` → `archive-revx-rev1.dwarfs` (only a numeric suffix counts)

## Decisions
- Revision unmounts the old archive. The old overlay is cleared only after the new archive exists, so a failed revision keeps all changes.
- Clearing the old overlay is best effort after the new archive exists. A failure there does not undo the revision.
- Flattening copies every file in full, as the overlay does.
- Scratch trees live next to the archive, so they are on the same filesystem as the output.
