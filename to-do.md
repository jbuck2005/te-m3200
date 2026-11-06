# To do

## Tag a release so device configs can pin it

My device configs currently pull this component from `main`:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/jbuck2005/te-m3200
      ref: main          # follows every push
      path: esphome/components
    components: [m3200]
    refresh: 1d
```

That means every push here ends up in the next device build. It's fine while
the component is still changing (sleep mode was added 2026-11-03). Once it
settles, pin a tag:

- [x] Check it's ready: no changes planned, and the devices running `main`
      look healthy (no stale or fault warnings in the logs).
- [x] Tag the commit the devices are running and push the tag:

      ```sh
      git tag -a v1.0.0 -m "First pinned release"
      git push origin v1.0.0
      ```

- [ ] Optionally, create a GitHub release from the tag with short notes:
      `gh release create v1.0.0 --generate-notes`.
- [ ] In each device config, change `ref: main` to `ref: v1.0.0` (through a
      PR), then reflash and confirm the readings.
- [x] Add a short "Versions" note to the README so other users pin a tag
      too, instead of `main`. (Done; the examples and READMEs point at `@v1.0.0`.)

## After pinning

Changes to this repo stop reaching the devices until I **move the tag** on
purpose:

1. Commit and test on `main`. To try changes on one device first, point
   that config at `ref: main` or at a local copy.
2. Tag the next version: `v1.0.1` for fixes, `v1.1.0` for new options,
   `v2.0.0` if existing YAML stops working.
3. Bump `ref:` in the device configs through a PR, then reflash.
