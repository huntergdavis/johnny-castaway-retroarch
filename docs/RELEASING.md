# Release procedure

Releases are assembled only from successful GitHub Actions artifacts tied to one
exact commit. The assembler downloads and validates the artifacts; it does not create
a tag, GitHub Release, commit, or push.

## Prerequisites

Use an Ubuntu workstation with GitHub CLI authentication for the public repository.
Install Bash, Python 3, GNU binutils, `file`, `findutils`, `zip`, and `unzip`. Confirm
the checkout includes the release commit and has no unexpected tracked changes:

```sh
gh auth status
git fetch origin main
git status --short
```

Before treating a commit as a release candidate:

1. Run `make test HOST_CC=gcc WARNINGS='-Wall -Wextra -Wpedantic -Werror'`.
2. Complete the menu and behavior acceptance checklist in
   `docs/RETROARCH_INTEGRATION.md`, including every visible Core Option and fallback.
3. Confirm `johnny_castaway_libretro.info` has the intended final version without a
   `-dev` suffix.
4. Push the candidate commit, then manually dispatch `.github/workflows/ci.yml` with
   `full_web_player=true` and `.github/workflows/console-cross.yml` at the same ref.
5. Wait for both runs to complete successfully. Record the full 40-character commit
   SHA, full CI run ID, and console run ID from their Actions URLs.

## Assemble and verify

Choose a new ignored directory below `build/release/`. The command fails if the output
already exists, either run is not successful at the exact SHA, the artifact sets do
not match the ten-source-artifact contract, a required validation fails, or original data is
found.

```sh
sha=0123456789abcdef0123456789abcdef01234567
ci_run=12345678901
console_run=12345678902
output=build/release/v0.1.2

scripts/assemble-release.sh \
  --sha "$sha" \
  --ci-run "$ci_run" \
  --console-run "$console_run" \
  --output "$output"
```

The output contains:

- `assets/`: sixteen tested ZIPs derived from ten Actions source artifacts. The PSP
  asset and six unified console frontend assets are directly installable ZIPs; the
  other nine assets wrap their source artifacts;
- `artifacts/`: the extracted source artifacts used to make those ZIPs;
- `SHA256SUMS`: hashes for the sixteen releasable ZIPs;
- `CONTENTS.sha256`: an exhaustive hash inventory of extracted artifact files;
- `INVENTORY.md`: run URLs, artifact IDs, sizes, hashes, contents, and validation
  evidence;
- `RELEASE_NOTES_DRAFT.md`: concise notes that must receive a human accuracy review.

Repeat the checksum checks independently:

```sh
(cd "$output/assets" && sha256sum -c ../SHA256SUMS)
(cd "$output/artifacts" && sha256sum -c ../CONTENTS.sha256)
```

Review `INVENTORY.md`, every release-note claim, licenses/notices, all sixteen filenames,
and the GitHub Actions logs. Confirm there are no local original data files beneath
the output. The Web ZIP must contain no `local-content/` directory.

The `johnny-castaway-console-cores` artifact also carries independently validated
Switch, 3DS, GameCube, Wii U, Vita, and PS2 install ZIPs under
`build/installable-frontends/out/`.
The assembler validates and promotes those six nested packages to direct release ZIPs,
turning ten Actions source artifacts into sixteen release assets. Do not claim device
execution until the platform gates in
`INSTALLABLE_FRONTENDS.md` are complete.

`--dry-run` exists only to exercise the assembler against matching current-contract
Actions runs whose metadata still ends in `-dev`. It marks the inventory and notes
non-publishable. Never use a dry-run output for a release; historical runs that emitted
only the old nine-source-artifact set intentionally fail the current contract.

## Publish only after explicit approval

The following command creates the GitHub Release and tag, so do not run it during
assembly or review. After replacing the example values with the independently checked
SHA, version, and output directory, obtain explicit publication approval and run:

```sh
version=v0.1.2
sha=0123456789abcdef0123456789abcdef01234567
output=build/release/v0.1.2
repository=$(gh repo view --json nameWithOwner --jq .nameWithOwner)

gh release create "$version" \
  --repo "$repository" \
  --target "$sha" \
  --title "Johnny Castaway libretro $version" \
  --notes-file "$output/RELEASE_NOTES_DRAFT.md" \
  "$output"/assets/*.zip \
  "$output/SHA256SUMS#SHA256SUMS"
```

Immediately inspect the public release page, download all assets into a fresh
directory, rerun `sha256sum -c SHA256SUMS`, and verify the published tag resolves to
the intended commit. If any check fails, stop distribution and correct the release;
do not silently replace binaries without updating notes and hashes.
