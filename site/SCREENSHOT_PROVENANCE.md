# Website screenshot provenance

The website uses three authentic composite frames produced by the Johnny Castaway
RetroArch Web player. They are not AI-generated, reconstructed, or copied from another
website. Each source run used the exact public-release source commit
`cd62e390882ace098e95eb72ba32cd021419f715`; its ignored browser artifacts remain local
test evidence rather than release inputs.

| Tracked website image | Local evidence source | SHA-256 | What it documents |
|---|---|---|---|
| `assets/screenshots/johnny-island.png` | `build/web-smoke-v014-cd62e39/game.png` | `b2ddf52f74e160386eceba8f11a48a940dc14e110d6e5a4a3e7ea2f1c4c3d561` | Automatic Story island composition in the exact release Web candidate |
| `assets/visitor-airplane.png` | `build/web-smoke-v014-cd62e39-visitor1/game.png` | `0c88c8f1de07740487002ccc46db57af008a5ad718b8563b70e7ce0d5dcd398c` | The Visitor 1 fixed chapter in the exact release Web candidate |
| `assets/story-options.png` | `build/web-smoke-v014-cd62e39/story-options-top.png` | `3236b7469f553a5eb5fe4c68a42436fd99fb4c980090e895cca64f536dcb99ef` | The port's Story settings integrated into RetroArch Core Options |

The captures are tracked only to identify and document the behavior and user interface
of this unofficial port. Copyright in the underlying Johnny Castaway characters,
artwork, animation, and original game presentation remains with the respective rights
holders. Those portions of the composite images are not offered under the project's
GPL license. RetroArch menu artwork retains its upstream licensing described in the
project's third-party notices.

No `RESOURCE.MAP`, `RESOURCE.001`, extracted sprite, source animation, proprietary
holiday artwork, or user-supplied sound file accompanies these PNGs. The Pages build
obtains its runnable player only from the checksum-pinned public v0.1.4 Web release and
does not use the local evidence directories above.

Machine-readable hashes, exact release asset metadata, and accessible image
descriptions are recorded in `release-v0.1.4.json` beside this document.
