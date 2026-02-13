# Release Process

This process defines how to produce a versioned MT-Lang compiler release.

## Preconditions

- All required checklist items in `ENGINE_READY.md` for target phase are complete.
- Working tree is clean except intentional release edits.
- `make check` passes.

## Steps

1. Choose version number
- Update `VERSION`.
- Add release section to `CHANGELOG.md` with date and highlights.

2. Verify artifacts
- Run:
```bash
make clean
make -j"$(nproc)" release
make check
```
- Verify:
```bash
./dist/mtc --version
./dist/mtc_lsp --help 2>/dev/null || true
```

3. Tag and publish
- Commit release changes.
- Create annotated tag:
```bash
git tag -a "v$(cat VERSION)" -m "mtc v$(cat VERSION)"
```
- Push commit + tag.

4. Post-release validation
- Confirm CI green on tag.
- Update downstream docs/consumers with new version.

## Rollback

- If release fails after tagging:
  - publish fix in `CHANGELOG.md`
  - create patch release with incremented version
  - do not mutate existing tag
