# Install with an agent

If you use Claude Code, Codex or a similar coding agent on your simulator PC,
it can install Mesmer for you and talk you through your first flight.
Open the agent in any folder on the Windows PC that runs X-Plane, and paste
the prompt below.

The agent will ask before it changes anything outside the plugin's own
folder, and it never touches the yoke: the yoke moves only when you choose
*Retry* in X-Plane yourself.

---

```text
Please install Mesmer, an X-Plane 12 plugin for the Brunner CLS-E
force-feedback yoke, on this Windows PC, then walk me through my first
flight. Its home is https://github.com/antialias/mesmer. Work step by
step, tell me what you find at each step, and stop and ask me whenever
something is not as described here.

Rules:
- Change nothing outside X-Plane 12's folder and a temporary folder.
- Never start X-Plane, CLS2Sim or anything else that talks to the yoke.
  Never send anything to the yoke.
- Ask me before moving or deleting any file that is not part of this plugin.
- Use PowerShell. Don't install other software.

1. Find X-Plane 12. Read %LOCALAPPDATA%\x-plane_install_12.txt; it lists
   X-Plane 12 folders, one per line. Keep the ones that contain X-Plane.exe.
   If there is more than one, ask me which. If there is none, ask me where
   X-Plane 12 is.

2. Check that X-Plane and CLS2Sim are not running (Get-Process; process
   names starting with X-Plane or CLS2Sim). If either is, ask me to close it
   and wait for me. CLS2Sim must stay closed while flying with this plugin,
   because the plugin needs the yoke's USB bridge to itself. If CLS2Sim
   starts with Windows, tell me how to turn that off.

3. Download the newest release, pre-releases included, from
   https://api.github.com/repos/antialias/mesmer/releases?per_page=100.
   GitHub doesn't list them in version order, so skip drafts and take the
   highest tag: compare vMAJOR.MINOR.PATCH as numbers, and a tag with
   -alpha.N, -beta.N or -rc.N after it is older than the same number
   without. It has exactly one .zip asset and a SHA256SUMS
   asset. Download both into a new, empty temporary folder. Tell me the
   release's tag.

4. Verify the zip: compute its SHA-256 with Get-FileHash and compare it with
   the zip's line in SHA256SUMS. If they differ, stop, install nothing, and
   tell me.

5. Unzip it into the temporary folder. It must contain a single folder,
   BrunnerDirect, with win_x64\BrunnerDirect.xpl, BrunnerDirect.ini and
   BrunnerDirect.profiles.json. Stop if it doesn't.

6. Install it as <X-Plane 12>\Resources\plugins\BrunnerDirect.
   If that folder already exists: copy my BrunnerDirect.ini and
   BrunnerDirect.feel.ini (if present) into the new BrunnerDirect folder so
   my settings are kept, then move the old folder to
   <X-Plane 12>\Mesmer-backups\BrunnerDirect-<UTC date and time>.
   Never leave a backup inside Resources\plugins: X-Plane loads every folder
   there.

7. Look for CLS2Sim's own X-Plane plugin,
   <X-Plane 12>\Resources\plugins\XPlane10Connector. If it is there, explain
   that Mesmer replaces it and that, left in place, it may hold X-Plane's
   pitch and roll. Ask me whether to move it to
   <X-Plane 12>\Mesmer-backups\XPlane10Connector-<UTC date and time>.
   Move it only if I say yes. Tell me how to move it back if I want CLS2Sim
   again.

8. Delete the temporary folder. Show me what is now in
   Resources\plugins\BrunnerDirect.

9. Walk me through my first flight, one step at a time, waiting for me to
   say "done" or describe what I see after each:
   a. Read me the safety summary from
      https://github.com/antialias/mesmer/blob/main/SAFETY.md in a few
      sentences: the yoke's motors can push hard, keep hands off while it
      centres, and how to stop it (Plugins > Yoke (Brunner) > Stop yoke
      motors, or a key bound to the command brunner_direct/stop_motors).
   b. Suggest I bind a key to brunner_direct/stop_motors in X-Plane's
      keyboard settings ("Stop yoke motors").
   c. Switch the yoke on, keep CLS2Sim closed, start X-Plane and load the
      Cessna 172 SP. The top left corner should read
      "Yoke stopped - Retry to resume".
   d. Hands off the yoke, then click that line (or Plugins > Yoke (Brunner)
      > Retry). It reads "Yoke centring - hands off", the yoke centres, and
      the line disappears. Pitch and roll now work.
   e. Fly a little with force feel off (the motors are off).
   f. Parked with the engine off, switch on Plugins > Yoke (Brunner) > Force
      feel. The yoke should ease forward: the elevator's weight. Then a
      run-up: pitch should firm up as the power comes up.
   If the status line shows anything else, look it up in
   https://github.com/antialias/mesmer/blob/main/docs/troubleshooting.md
   and tell me what it means and what to do. If I need X-Plane's log, it is
   <X-Plane 12>\Log.txt; the plugin's lines start with [BrunnerDirect].

10. At the end, summarise what you changed, where the backups are, and how
    to uninstall (quit X-Plane, delete Resources\plugins\BrunnerDirect, and
    move XPlane10Connector back if I want CLS2Sim).
```

---

Prefer a script? [install.ps1](install.ps1) does steps 1 to 8 without an
agent. By hand: [docs/install.md](docs/install.md).
