#!/usr/bin/env bash
# Have a signing repository sign this commit's macOS plugin and fetch the result. Specification:
# docs/release-signing.md (the source of truth for the contract with the signing repository and for
# every failure here). Tests: tests/test-fetch-signed-plugin.sh.
#
#   src/scripts/fetch_signed_plugin.sh --signing-repo OWNER/NAME --repo OWNER/NAME --sha SHA --out DIR \
#       [--timeout-minutes N] [--help]
#
# Starts the signing workflow, waits for its run, downloads the artifact `signed-plugin` and checks it
# really is the signed plugin for this repository and commit. GH_TOKEN must be a token that may start
# workflows and read artifacts in the signing repository (the release workflow passes the secret
# MACOS_SIGNING_TOKEN). On success --out holds BrunnerDirect.xpl and provenance.json; on any failure it
# is left empty. Written for bash 3.2 and Linux; needs gh and jq.
set -uo pipefail

WORKFLOW="sign-mesmer.yml"
ARTIFACT="signed-plugin"
# Test knobs, not for users: how often to poll, and how long to wait for the run to appear.
POLL_SECONDS="${FETCH_POLL_SECONDS:-20}"
FIND_SECONDS="${FETCH_FIND_SECONDS:-180}"

signing_repo="" repo="" sha="" out=""
timeout_minutes=60

usage() {
    sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    cat <<EOF

  --signing-repo OWNER/NAME   the repository whose $WORKFLOW signs
  --repo OWNER/NAME           the repository being released
  --sha SHA                   the release commit (the full 40-character SHA)
  --out DIR                   where the verified files are put
  --timeout-minutes N         how long to wait for the signing run (default $timeout_minutes)
EOF
}

die() { echo "fetch_signed_plugin.sh: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --signing-repo|--repo|--sha|--out|--timeout-minutes)
            [ $# -ge 2 ] || die "$1 needs a value"
            case "$1" in --signing-repo) signing_repo="$2" ;; --repo) repo="$2" ;; --sha) sha="$2" ;; --out) out="$2" ;; --timeout-minutes) timeout_minutes="$2" ;; esac
            shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
done

for need in signing_repo repo sha out; do
    [ -n "${!need}" ] || die "--$(echo "$need" | tr '_' '-') is required"
done
echo "$sha" | grep -E -q '^[0-9a-f]{40}$' || die "--sha must be a full 40-character commit SHA"
echo "$timeout_minutes" | grep -E -q '^[0-9]+$' || die "--timeout-minutes must be a whole number"
[ -n "${GH_TOKEN:-}" ] || die "GH_TOKEN is empty: the release needs the secret MACOS_SIGNING_TOKEN (a token with Actions read and write on $signing_repo) when MACOS_SIGNING_REPO is set"
for tool in gh jq shasum; do command -v "$tool" >/dev/null 2>&1 || { [ "$tool" = shasum ] && command -v sha256sum >/dev/null 2>&1; } || die "$tool not found"; done
sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'; else shasum -a 256 "$1" | awk '{print $1}'; fi; }
iso() { date -u -d "@$1" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date -u -r "$1" +%Y-%m-%dT%H:%M:%SZ; }

rm -rf "$out"; mkdir -p "$out" || die "cannot make $out"
work="$(mktemp -d "${TMPDIR:-/tmp}/fetch-signed.XXXXXX")" || die "cannot make a temporary folder"
trap 'rm -rf "$work"' EXIT
fail() { rm -rf "$out"; mkdir -p "$out"; die "$*"; }

title="sign $repo@$sha"
start_epoch="$(date +%s)"
since="$(iso $((start_epoch - 30)))"   # a little slack for clock skew; the title already names the commit

gh workflow run "$WORKFLOW" -R "$signing_repo" -f "repo=$repo" -f "ref=$sha" >/dev/null 2>"$work/err" \
    || fail "could not start $WORKFLOW in $signing_repo: $(head -c 300 "$work/err") (is the token valid and allowed to run workflows there?)"
echo "started $WORKFLOW in $signing_repo for $repo@$sha"

# Find our run by its title.
run_id=""
find_deadline=$((start_epoch + FIND_SECONDS))
while [ -z "$run_id" ] && [ "$(date +%s)" -lt "$find_deadline" ]; do
    run_id="$(gh run list -R "$signing_repo" --workflow "$WORKFLOW" --event workflow_dispatch --limit 30 --json databaseId,displayTitle,createdAt 2>/dev/null \
        | jq -r --arg t "$title" --arg s "$since" '[.[] | select(.displayTitle==$t and .createdAt>=$s)] | sort_by(.createdAt) | last | .databaseId // empty')"
    [ -n "$run_id" ] || sleep "$POLL_SECONDS"
done
[ -n "$run_id" ] || fail "no run titled '$title' appeared in $signing_repo within $FIND_SECONDS seconds"
run_url="https://github.com/$signing_repo/actions/runs/$run_id"
echo "waiting for $run_url"

# Wait for it to finish.
deadline=$((start_epoch + timeout_minutes * 60))
status="" conclusion=""
while :; do
    info="$(gh run view "$run_id" -R "$signing_repo" --json status,conclusion 2>/dev/null)"
    status="$(echo "$info" | jq -r '.status // empty')"
    conclusion="$(echo "$info" | jq -r '.conclusion // empty')"
    [ "$status" = completed ] && break
    [ "$(date +%s)" -lt "$deadline" ] || fail "the signing run did not finish within $timeout_minutes minutes: $run_url"
    sleep "$POLL_SECONDS"
done
[ "$conclusion" = success ] || fail "the signing run ended '$conclusion', not success: $run_url"

# Download and check what it made.
gh run download "$run_id" -R "$signing_repo" -n "$ARTIFACT" -D "$work/dl" >/dev/null 2>"$work/err" \
    || fail "could not download the artifact $ARTIFACT of $run_url: $(head -c 300 "$work/err")"
for f in BrunnerDirect.xpl BrunnerDirect.xpl.sha256 provenance.json; do
    [ -s "$work/dl/$f" ] || fail "the artifact has no $f ($run_url)"
done
got_repo="$(jq -r '.source.repository // empty' "$work/dl/provenance.json")"
got_sha="$(jq -r '.source.commit // empty' "$work/dl/provenance.json")"
notarized="$(jq -r '.notarized // empty' "$work/dl/provenance.json")"
[ "$got_repo" = "$repo" ] || fail "the signed plugin is for repository '$got_repo', not $repo ($run_url)"
[ "$got_sha" = "$sha" ] || fail "the signed plugin is for commit '$got_sha', not $sha ($run_url)"
[ "$notarized" = true ] || fail "provenance.json does not say the plugin was notarized ($run_url)"
want="$(awk '{print $1; exit}' "$work/dl/BrunnerDirect.xpl.sha256")"
have="$(sha256 "$work/dl/BrunnerDirect.xpl")"
[ "$want" = "$have" ] || fail "the signed plugin's SHA-256 is $have, its .sha256 file says $want ($run_url)"
gh attestation verify "$work/dl/BrunnerDirect.xpl" --repo "$signing_repo" >/dev/null 2>"$work/err" \
    || fail "the plugin is not attested by $signing_repo: $(head -c 300 "$work/err")"

cp "$work/dl/BrunnerDirect.xpl" "$work/dl/provenance.json" "$out/" || fail "cannot copy the verified files to $out"
echo "signed plugin for $repo@$sha verified: $have  (signing run: $run_url)"
