# Signing the macOS plugin in a release: specification

A downloaded macOS plugin that is only ad hoc signed (what `build_macos.sh` makes) is blocked by
Gatekeeper until the user removes the quarantine flag. A release can instead ship a plugin signed with a
Developer ID and notarized by Apple, made by a **separate signing repository** so the key is never
reachable from this project. `.github/workflows/release.yml` can ask that repository for it;
`src/scripts/fetch_signed_plugin.sh` does the asking; `tests/test-fetch-signed-plugin.sh` checks each
statement here.

**Off by default.** With the repository variable `MACOS_SIGNING_REPO` unset, a release is exactly as
[release-packaging.md](release-packaging.md) describes, with the ad hoc macOS plugin.

## Switching it on

| Setting | Where | Value |
|---|---|---|
| `MACOS_SIGNING_REPO` | repository variable | `OWNER/NAME` of the signing repository, e.g. `eklynx/mesmer-buildtools` |
| `MACOS_SIGNING_TOKEN` | repository secret | a token that may start workflows and read artifacts in that repository |

Create the token as a fine-grained personal access token (Settings, Developer settings): resource owner
the signing repository's owner, **only** the signing repository, permission **Actions: read and write**,
nothing else, an expiry of about a year. It only lets a holder start signing runs for the signing
repository's allowlisted source repositories and read what they produce; it cannot read the signing key.
When it expires, the next release fails at `sign-macos` with a message naming the secret: make a new one
and `gh secret set MACOS_SIGNING_TOKEN`.

## What a release does when it is on

1. `plan` sets `sign_macos` (macOS enabled and `MACOS_SIGNING_REPO` set; a value that is not
   `OWNER/NAME` stops the release).
2. `build-macos` builds and tests as usual. Then `sign-macos` (only after those tests passed, so only a
   commit that tests green is signed) runs `fetch_signed_plugin.sh` with the token: it starts the signing
   workflow for this repository and commit, waits for its run, downloads the artifact and checks it.
3. `package` ships the signed plugin in place of the ad hoc one, requires `sign-macos` to have succeeded,
   and adds a line to the release notes naming the signer and the signing run.

If signing is on and anything goes wrong, the release fails and publishes nothing. There is no fallback to
the ad hoc plugin.

## The contract with a signing repository

Any repository that follows this can be named in `MACOS_SIGNING_REPO` (the reference one is
[mesmer-buildtools](https://github.com/eklynx/mesmer-buildtools), whose `docs/signing.md` is the other half):

- It has a workflow `sign-mesmer.yml`, started with the inputs `repo` (the repository being released) and
  `ref` (the **name** of the tag or branch being released; a bare commit SHA must be refused, because a SHA
  can be resolved from another fork of the repository's network). The caller, which knows the commit it
  expects, checks the result's `provenance.json` against it.
- The run's title is `sign <repo>@<ref>`, which is how the script finds its own run.
- On success the run uploads the artifact `signed-plugin` holding `BrunnerDirect.xpl`,
  `BrunnerDirect.xpl.sha256` and `provenance.json` with `source.repository`, `source.ref`, `source.commit`,
  `notarized`, `signer.authority` and `run`.
- The file carries a build provenance attestation from the signing repository.

## The script

```
src/scripts/fetch_signed_plugin.sh --signing-repo OWNER/NAME --repo OWNER/NAME --ref NAME --sha SHA --out DIR [--timeout-minutes N]
```

`--ref` is the tag being released (the release passes `$GITHUB_REF_NAME`) and `--sha` the commit it must be at
(`$GITHUB_SHA`). `GH_TOKEN` is the token. On success `--out` holds the verified `BrunnerDirect.xpl` and `provenance.json`.
Every failure prints `fetch_signed_plugin.sh: <reason>`, exits non-zero and leaves `--out` empty:

| Cause | Reason says |
|---|---|
| Unknown option, missing option, a SHA that is not 40 hex characters, a ref that is a SHA or not a plain name | `unknown option`, `--x is required`, `full 40-character`, `not a commit SHA` |
| `GH_TOKEN` empty | names `MACOS_SIGNING_TOKEN` |
| The signing workflow cannot be started (bad or unauthorised token) | `could not start sign-mesmer.yml` |
| No run with the title appears within 3 minutes | `no run titled 'sign REPO@REF' appeared` |
| The run does not finish within `--timeout-minutes` (default 60) | `did not finish within N minutes`, with the run URL |
| The run ends in anything but success | `ended 'X', not success`, with the run URL |
| The artifact is missing or lacks a file | `could not download the artifact`, `has no FILE` |
| `provenance.json` names another repository, ref or commit, or `notarized` is not true | `is for repository 'X'`, `is for ref 'X'`, `is for commit 'X'`, `does not say ... notarized` |
| The file's SHA-256 differs from its `.sha256` | `SHA-256 is X, its .sha256 file says Y` |
| The attestation is missing or from another repository | `is not attested by OWNER/NAME` |

## Adopting it upstream

The same workflow and scripts can live in a project that does not use this signing repository. It stays
off. A maintainer who wants signed releases sets `MACOS_SIGNING_REPO` to a signing repository that follows
the contract (their own, or one they trust to build their commits) and stores a token for it as
`MACOS_SIGNING_TOKEN`. Whoever can push a release tag to the project can then cause that repository to sign
the tagged commit, so the signing repository should accept only the project's repositories and only
reviewed (main or tagged) commits, as mesmer-buildtools does.
