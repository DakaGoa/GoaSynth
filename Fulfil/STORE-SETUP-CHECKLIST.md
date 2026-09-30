# GoaSynth × Lemon Squeezy — product setup checklist

The working, fill-in copy of `Fulfil/CHECKOUT.md` §3. Sit in the Lemon Squeezy
dashboard with this open, fill the blanks, tick the boxes. About 15 minutes
plus the site wiring in §3 below.

Filled values are the ones already committed to this repository — the legal
pages and the site CONFIG expect exactly these. Don't invent alternatives.

Store slug (from your store's URLs): ______________________________________
Date set up: ______________________________________

---

## 0. Before the product (one-time, no recovery if skipped)

- [ ] Lemon Squeezy account created; store named — the slug above is the
      `<yourstore>` in every URL
- [ ] Payout / bank details entered
- [ ] Keygen initialized once: `GoaSynthKeygen --init`
- [ ] **Keypair backed up** — copy `%APPDATA%\GoaSynth\Keygen\` into your
      password manager. Date backed up: ______________________________
      (Lost = no licence can ever be issued again. Leaked = rotate, rebuild,
      republish. There is no third option.)

## 1. Create the product (§3)

| Tick | Field | Set exactly |
| --- | --- | --- |
| [ ] | Name | `GoaSynth — Goa Trance Synthesizer (VST3)` |
| [ ] | Type | Digital download · one-time payment · no subscription |
| [ ] | Variants | None — or a single "Personal licence — 3 machines" |
| [ ] | Price | **€15** in EUR |
| [ ] | Prices include tax | **On** — €15 is what everyone pays, everywhere |
| [ ] | Generate licence keys | **Off** — the store's keys activate nothing; your `GoaSynthKeygen` signs the real ones |
| [ ] | Custom field | `Machine ID (20 characters, from the plugin's activation screen)` — **required** |
| [ ] | Delivery | Hosted file: the release ZIP (§1b below) |
| [ ] | Refund window | 14 days — matches the site's refund policy |
| [ ] | Receipt + support email | `goasynth.support@gmail.com` |
| [ ] | Tax category | Digital goods / software |
| [ ] | Checkout questions | Email + country (from the store) + the machine-ID field. **Do not** ask for DAW or OS |

### 1b. Upload the deliverable

- [ ] Upload `dist/GoaSynth-1.1.0-win64.zip` (12.1 MB) as the hosted file
- [ ] Verify the upload: `Get-FileHash .\GoaSynth-1.1.0-win64.zip -Algorithm SHA256`
      must equal
      `6bbbca8b6934e7cd6279b26e5c4cf8a95c97bc668f037832ca2512c2a9cb4d14`
      (the number published at `downloads/SHA256SUMS.txt` — buyers check
      against it, so the stored file must match byte for byte)
- [ ] After every future release: re-run `python tools/make-release.py`,
      re-upload, re-verify the new hash, and commit the regenerated checksums

## 2. Copy the URLs

From the product's *Share* page, write these down:

- Buy link (→ `CONFIG.checkout`):
  `https://________________________.lemonsqueezy.com/buy/________________________`
- Customer library (→ `CONFIG.download`):
  `https://________________________.lemonsqueezy.com/my-orders`
- Test-mode checkout link (for §4, do not ship to buyers):
  ____________________________________________________________

## 3. Wire the site

In `docs/app.js`, replace the TODOs:

```js
checkout: 'https://<yourstore>.lemonsqueezy.com/buy/<uuid>',   // from §2
download: 'https://<yourstore>.lemonsqueezy.com/my-orders',    // from §2
machineIdField: true,   // the checkout custom field now exists
```

- [ ] Both URLs pasted, `machineIdField` flipped to `true`, TODOs removed
- [ ] Committed and pushed; waited for the GitHub Pages deploy
- [ ] Live site checked: Buy buttons open the Lemon Squeezy checkout, the
      order-email fallback is gone, and the pricing card shows the
      machine-ID note
- [ ] `ctest -R DocsCheck` still green (the drift guard reads this CONFIG)

## 4. Dress rehearsal (test mode — before announcing)

- [ ] Run the plugin once, copy the MACHINE ID from the activation screen
- [ ] Buy your own product in **test mode**, entering that machine ID
- [ ] Export the order (Orders → Export CSV) into an orders folder
- [ ] `GoaSynthFulfil --inbox <orders> --out <out> --dry-run` — machine ID found
- [ ] Real run without `--dry-run` — `.goalicense` + reply `.eml` written
- [ ] Open the `.goalicense` on a clean machine/copy → plugin activates
- [ ] Refund the test order → `GoaSynthKeygen --verify <serial>` reports it
      revoked with the order number as the reason
- [ ] Switch the store from test mode to live

## 5. Go live

- [ ] Store live, ZIP uploaded and hash-verified (§1b)
- [ ] Site in live-store mode (§3)
- [ ] `goasynth.orders@gmail.com` and `goasynth.support@gmail.com` watched
- [ ] `GoaSynthFulfil --watch --interval 60` running (or a pass per morning)
      — see `Fulfil/CHECKOUT.md` §3b for the daily flow

---

The full playbook — why a merchant of record, the store comparison, the
fulfilment workflow, refunds and transfers — is `Fulfil/CHECKOUT.md`. This
file is just its §3 with ticks and blanks.
