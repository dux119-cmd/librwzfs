# Archive creation and revision

Both creation and revision write archives in-process with the dwarfs writer. No `mkdwarfs` binary is
run, and no scratch tree is made.

## create_archive
`rwz::create_archive(source_dir, output, options)` archives a host directory. It runs the
writer's scanner over the tree, with the same settings as the `mkdwarfs` CLI flags
`--categorize --no-history --compression=<compression>`:

- **Categorization** (`--categorize`): the default categorizers, `fits`, `pcmaudio`, `binary` and
  `incompressible`, with their default options.
- **History** (`--no-history`): off. No history section is written.
- **Compression**: `options.compression`, default `zstd:level=12`. Metadata sections use
  zstd.
- **Segmenter defaults**: the per-category values `mkdwarfs` uses for its default level.

It writes `<output>.tmp` and renames it to `<output>` on success. A failed run removes the temp file.

Error values: `source_missing` (no such directory), `tool_failed` (the writer threw, e.g. for an unknown
compression option; the exception text goes to the diagnostic stream), `io`, `unsupported_platform`
(a build without the dwarfs writer).

## create_archive_from_view
`rwz::create_archive_from_view(mountpoint, output, options)` archives the merged view mounted at
`mountpoint` (archive plus overlay). Every path goes through the library's own calls: mounted paths
are answered from the merged view, and paths outside the mount fall back to the host. Permissions,
modification times, symlinks and contents are preserved; no owners are stored.

## revise_archive
`rwz::revise_archive(mountpoint, options)` revises the archive mounted at `mountpoint`:

1. Requires an archive mounted at exactly `mountpoint` with a non-empty overlay. Otherwise `no_changes` or `mount_not_found`.
2. Writes `<stem>-revN<ext>` next to the archive with `create_archive_from_view`. The view is still mounted.
3. Unmounts (which releases the overlay lock) and clears the old overlay's `data/`, `whiteouts/` and `opaque/`.
4. Returns the new archive path. The caller mounts it to continue.

Naming: the stem is the archive file name up to its first dot, as in the overlay rule.
- `archive.dwarfs` → `archive-rev1.dwarfs`
- `archive-rev1.dwarfs` → `archive-rev2.dwarfs`
- `archive-revx.dwarfs` → `archive-revx-rev1.dwarfs` (only a numeric suffix counts)

## Decisions
- Revision unmounts the old archive. The old overlay is cleared only after the new archive exists, so a failed revision keeps all changes.
- Clearing the old overlay is best effort after the new archive exists. A failure there does not undo the revision.
- Creation and revision copy each file's content once, straight from the source (host tree or merged view) into the output. Nothing is staged on disk first.
- The writer requires every `file_stat` field to be set. Owners are zero (no owners are stored); the view reports the mounting process's owner.
