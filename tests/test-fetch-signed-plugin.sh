#!/usr/bin/env bash
# Tests src/scripts/fetch_signed_plugin.sh against docs/release-signing.md. No network: a fake `gh`
# plays the signing repository (it starts a run, lists it, reports its status, hands over an artifact
# and attests it, each controllable from the sandbox). Runs on Linux and macOS.
#   tests/test-fetch-signed-plugin.sh
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
failures=0
check() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; failures=$((failures+1)); fi; }

sandbox="$(mktemp -d "${TMPDIR:-/tmp}/mesmer-fetch.XXXXXX")"
trap 'rm -rf "$sandbox"' EXIT
fetch="$ROOT/src/scripts/fetch_signed_plugin.sh"
SHA=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
REF=v1.2.3
OTHER_SHA=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
REPO=eklynx/mesmer
SIGNING=eklynx/mesmer-buildtools

# The fake gh. State lives in $FAKE: calls.log, artifact/ (what a download hands over), and flags.
FAKE="$sandbox/fake"; mkdir -p "$sandbox/bin" "$FAKE"
cat > "$sandbox/bin/gh" <<'GH'
#!/usr/bin/env bash
echo "$*" >> "$FAKE/calls.log"
now() { date -u +%Y-%m-%dT%H:%M:%SZ; }
later() { e=$(( $(date +%s) + 5 )); date -u -d "@$e" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date -u -r "$e" +%Y-%m-%dT%H:%M:%SZ; }
case "$1 $2" in
    "workflow run") [ -e "$FAKE/dispatch-fails" ] && { echo "HTTP 403: Resource not accessible by personal access token" >&2; exit 1; }; exit 0 ;;
    "run list")
        [ -e "$FAKE/no-run" ] && { echo "[]"; exit 0; }
        title="${FAKE_TITLE_OVERRIDE:-$(cat "$FAKE/title")}"
        printf '[{"databaseId":111,"displayTitle":"sign other/repo@%s","createdAt":"%s"},{"databaseId":222,"displayTitle":"%s","createdAt":"%s"},{"databaseId":99,"displayTitle":"%s","createdAt":"2000-01-01T00:00:00Z"}]\n' "$FAKE_SHA" "$(later)" "$title" "$(now)" "$title" ;;
    "run view")
        n=$(cat "$FAKE/views" 2>/dev/null || echo 0); echo $((n+1)) > "$FAKE/views"
        if [ -e "$FAKE/never-finishes" ]; then echo '{"status":"in_progress","conclusion":null}'
        elif [ "$n" -lt 1 ]; then echo '{"status":"in_progress","conclusion":null}'
        else printf '{"status":"completed","conclusion":"%s"}\n' "$(cat "$FAKE/conclusion")"; fi ;;
    "run download")
        while [ $# -gt 0 ]; do [ "$1" = "-D" ] && dest="$2"; shift; done
        [ -e "$FAKE/no-artifact" ] && { echo "no artifact" >&2; exit 1; }
        mkdir -p "$dest" && cp -R "$FAKE/artifact/." "$dest/" ;;
    "attestation verify") [ -e "$FAKE/attest-fails" ] && { echo "no attestation found" >&2; exit 1; }; exit 0 ;;
esac
exit 0
GH
chmod +x "$sandbox/bin/gh"
export PATH="$sandbox/bin:$PATH" FAKE FAKE_SHA="$SHA"
export FETCH_POLL_SECONDS=0 FETCH_FIND_SECONDS=2
export GH_TOKEN=test-token

sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'; else shasum -a 256 "$1" | awk '{print $1}'; fi; }

# reset [key=value...]: a good world, then optional breakages.
reset() {
    rm -rf "$FAKE"/*; mkdir -p "$FAKE/artifact"
    echo "sign $REPO@$REF" > "$FAKE/title"
    echo success > "$FAKE/conclusion"
    echo "signed plugin bytes" > "$FAKE/artifact/BrunnerDirect.xpl"
    echo "$(sha256 "$FAKE/artifact/BrunnerDirect.xpl")  BrunnerDirect.xpl" > "$FAKE/artifact/BrunnerDirect.xpl.sha256"
    printf '{"source":{"repository":"%s","ref":"%s","commit":"%s"},"notarized":true,"signer":{"team":"T"}}\n' "$REPO" "$REF" "$SHA" > "$FAKE/artifact/provenance.json"
}
run() { out="$("$fetch" --signing-repo "$SIGNING" --repo "$REPO" --ref "$REF" --sha "$SHA" --out "$sandbox/out" "$@" 2>&1)"; code=$?; }
says() { echo "$out" | grep -q -e "$1"; }
out_empty() { [ -d "$sandbox/out" ] && [ -z "$(ls -A "$sandbox/out")" ]; }

# Spec: usage.
reset; out="$("$fetch" --help 2>&1)"; code=$?; [ $code -eq 0 ] && echo "$out" | grep -q -e "--signing-repo OWNER/NAME"; check $? "--help exits 0"
out="$("$fetch" --bogus 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "unknown option: --bogus"; check $? "an unknown option is rejected"
out="$("$fetch" --repo $REPO --ref $REF --sha $SHA --out "$sandbox/out" 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q -e "--signing-repo is required"; check $? "a missing option is rejected"
out="$("$fetch" --signing-repo $SIGNING --repo $REPO --ref $REF --sha abc --out "$sandbox/out" 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "full 40-character"; check $? "a short SHA is rejected"
out="$("$fetch" --signing-repo $SIGNING --repo $REPO --ref $SHA --sha $SHA --out "$sandbox/out" 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "not a commit SHA"; check $? "a SHA given as the ref is rejected"
out="$("$fetch" --signing-repo $SIGNING --repo $REPO --sha $SHA --out "$sandbox/out" 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q -e "--ref is required"; check $? "a missing --ref is rejected"

# Spec: no token, no run, and the message names the secret.
reset; out="$(GH_TOKEN= "$fetch" --signing-repo $SIGNING --repo $REPO --ref $REF --sha $SHA --out "$sandbox/out" 2>&1)"; code=$?
[ $code -ne 0 ] && echo "$out" | grep -q "MACOS_SIGNING_TOKEN" && [ ! -s "$FAKE/calls.log" ]; check $? "an empty token is refused, names MACOS_SIGNING_TOKEN, and nothing is called"

# Spec: the happy path starts the run for this repo and commit, and leaves exactly the verified files.
reset; run
[ $code -eq 0 ] && says "verified" && [ -f "$sandbox/out/BrunnerDirect.xpl" ] && [ -f "$sandbox/out/provenance.json" ] && [ "$(ls "$sandbox/out" | wc -l | tr -d ' ')" = 2 ]; check $? "success: the signed plugin and provenance.json are in --out"
grep -q "^workflow run sign-mesmer.yml -R $SIGNING -f repo=$REPO -f ref=$REF\$" "$FAKE/calls.log"; check $? "it starts sign-mesmer.yml in the signing repo with this repo and the ref NAME, never the SHA"
grep -q "^run download 222 -R $SIGNING -n signed-plugin" "$FAKE/calls.log"; check $? "it downloads the run whose title matches, not the other runs listed"
grep -q "^attestation verify .*--repo $SIGNING" "$FAKE/calls.log"; check $? "it verifies the attestation against the signing repo"

# Spec: failures leave --out empty and say why.
fails() { local label="$1" pattern="$2"; shift 2; run "$@"; [ $code -ne 0 ] && says "$pattern" && out_empty; check $? "$label"; }
reset; touch "$FAKE/dispatch-fails"; fails "a refused dispatch is reported with the token hint" "could not start sign-mesmer.yml"
reset; touch "$FAKE/no-run"; fails "a run that never appears is reported" "no run titled 'sign $REPO@$REF' appeared"
reset; echo failure > "$FAKE/conclusion"; fails "a failed signing run fails the fetch and gives its URL" "ended 'failure', not success: https://github.com/$SIGNING/actions/runs/222"
reset; touch "$FAKE/never-finishes"; fails "a run that does not finish in time is reported" "did not finish within 0 minutes" --timeout-minutes 0
reset; touch "$FAKE/no-artifact"; fails "a missing artifact is reported" "could not download the artifact signed-plugin"
reset; rm "$FAKE/artifact/provenance.json"; fails "an artifact without provenance.json is rejected" "has no provenance.json"
reset; sed -i.bak "s/\"ref\":\"$REF\"/\"ref\":\"v9.9.9\"/" "$FAKE/artifact/provenance.json"; fails "a plugin signed for another ref is rejected" "is for ref 'v9.9.9'"
reset; sed -i.bak "s/\"commit\":\"$SHA\"/\"commit\":\"$OTHER_SHA\"/" "$FAKE/artifact/provenance.json"; fails "a plugin signed for another commit is rejected" "is for commit '$OTHER_SHA'"
reset; sed -i.bak "s#$REPO#someone/else#" "$FAKE/artifact/provenance.json"; fails "a plugin signed for another repository is rejected" "is for repository 'someone/else'"
reset; sed -i.bak 's/"notarized":true/"notarized":false/' "$FAKE/artifact/provenance.json"; fails "a plugin that is not notarized is rejected" "does not say the plugin was notarized"
reset; echo "tampered" >> "$FAKE/artifact/BrunnerDirect.xpl"; fails "a plugin whose hash does not match is rejected" "SHA-256 is"
reset; touch "$FAKE/attest-fails"; fails "a plugin without an attestation is rejected" "is not attested by $SIGNING"

[ "$failures" -eq 0 ] && echo "all fetch_signed_plugin tests passed" || { echo "$failures failed"; exit 1; }
