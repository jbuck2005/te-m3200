# CLAUDE.md

Standing rules, decisions and open items for AI assistants working in this repository.
Keep each entry short and say *why*, so a later session doesn't undo a decision it
doesn't understand.

## Commits and pull requests

- Never credit Claude or any AI assistant: no `Co-Authored-By` or session trailers, no
  session links, no "Generated with" lines, and no AI author or committer, in commits,
  PRs or files. Commits show only the maintainer.
- Author and committer email: `59450931+jbuck2005@users.noreply.github.com`. Set it
  before the first commit, or the commit is authored by the assistant.
- Name branches `dev/<topic>`. Never put "claude" in a branch name: merging copies it
  into the history of `main`.
- Keep "claude" and "anthropic" out of commit messages and PR titles, including for
  changes to this file (write "project rules").
- Stage files by name. Never `git add -A` or `git add .`.
- Never force-push `main`.

## Privacy

- The repo is public. Nothing home-specific belongs in it: no IPs, hostnames, paths,
  names or credentials. Use `esphome/secrets.example.yaml` placeholders.
- `esphome/secrets.yaml` holds real secrets and is git-ignored. Never commit it.

## How changes are delivered

- **One PR per change, and the owner merges it.** Never merge your own PR: `main`
  reaches real devices, and nothing else checks a change before it's public.
- **Use merge commits, not squash.** A squash merge once broke later pushes to
  a reused branch. Merge commits take the PR title.
- **PR titles say what the change does.** Never a step number or a generic
  label. Start with a scope prefix: `Arduino: …`, `ESPHome: …`,
  `Arduino + ESPHome: …`, `Docs: …`.
- **Document design choices with the reason.** Put the *why* in the README,
  in code comments, or both, next to the thing it explains.
- **CI runs the checks on every PR** (`.github/workflows/ci.yml`, added 2026-11-09).
  The earlier "no CI, it's over budget" rule is gone: Actions minutes are free on
  public repos. A PR is ready only when CI is green. It runs:
  - `tests/run.sh` (host-side unit tests for the decoder and the Arduino class)
  - `ruff check` on `esphome/components`
  - Arduino build of both examples for `uno` and `esp32dev` (`pio ci`)
  - `esphome config` and `esphome compile` on `tests/esphome/test.*.yaml`
    (ESP32 IDF + Arduino, ESP8266, RP2040), built from the checkout, not GitHub
  Run `tests/run.sh` locally before pushing anyway; it takes seconds.
- **Keep `tests/esphome/common.yaml` using every option.** When a YAML option is
  added to `sensor.py`, add it there too, or CI never compiles its codegen.
- **Assistants can't test on real hardware.** Say so in PRs that change driver
  behaviour.

## Who is affected by a merge

The owner's devices build the ESPHome component from `main` with
`refresh: 1d`. Every merge to `main` reaches real hardware within a day.
Treat `main` as live until the configs are pinned to a tag (see
[to-do.md](to-do.md)).

## Decisions

### Keep the datasheet A20 endpoint tolerances (2026-11-04)

Datasheet rev A21 narrows the zero/full-scale count limits to 790–1210 and
14790–15210. The drivers keep rev A20's wider limits, 750–1250 and
14720–15250, on purpose. Don't tighten them.

The full reasoning is in the README section "Datasheet revisions and the
endpoint tolerances", and is repeated in `esphome/components/m3200/sensor.py`,
`arduino/M3200/src/M3200.h` and `esphome/README.md`. Revisit this only if TE
changes the typical counts (1000 at zero, 15000 at full scale).

## Datasheets and te.com

- `docs/` holds datasheet revs A20 and A21 and the standalone "Interfacing to
  Digital Pressure Modules" guide rev A3. A21's page numbers match A20's.
- Datasheet pages 11–17 carry the digital interfacing section, with the same
  §1.1–§2.1 numbering as the standalone guide. The guide has no te.com link of
  its own.
- The README links TE's always-latest datasheet for part `20006465-00`. That
  link doesn't ask for an email address. Some other TE DocumentDelivery links
  do, so prefer this one.
- TE's CDN returns HTTP 403 to cloud servers, so a cloud session can't download
  the PDFs. New copies have to come from the owner.

## Open items

- **Pin the device configs.** The owner's devices still build from `main`. Steps are
  in [to-do.md](to-do.md).
