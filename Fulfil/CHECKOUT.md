# Setting up the GoaSynth checkout

GoaSynth is sold as a **€15 one-time personal licence**. This is the playbook for putting a
**merchant of record** behind the Buy buttons, and for wiring the resulting URL into the site.

Nothing here needs a server: the site is static, the store hosts the checkout *and* the download — and
the plugin itself activates **offline**, so there is no licence server to run either.

> **Read this first.** The build you sell is **locked until activated**, and a serial is signed for the
> buyer's **machine ID**, so a stock "pay → download ZIP" flow cannot deliver a working plugin on its own.
> You need two things working before you take money:
>
> 1. **A machine-ID field at checkout** — §3; and
> 2. **A keygen run per buyer** — §3b, about 30 seconds of work per sale.
>
> If you would rather not do that yet, the honest alternative is to ship a build with the activation gate
> removed (sold as-is, no serial), which is a different product than the one this site describes.

---

## 1. Why a merchant of record, and not raw Stripe

A merchant of record (MoR) becomes the **seller of record**. That means the store, not you, is
responsible for:

- registering for and remitting **VAT/sales tax** in every country you sell into (EU VAT, UK VAT,
  US state tax, and so on);
- issuing **compliant invoices and receipts**, in the buyer's language and currency;
- handling **card data** — so PCI compliance is the store's problem, not yours;
- chargebacks, fraud screening and refund processing.

With plain Stripe you are the seller: you would have to register for VAT yourself (in the EU, via
One-Stop-Shop) and file in each jurisdiction. For a one-person plugin shop that is a lot of unpaid
bookkeeping. The MoR's cut is what you pay to never think about it.

## 2. Which store

| | Lemon Squeezy | Paddle | Gumroad |
| --- | --- | --- | --- |
| Fee per sale (published rate, check before signing up) | 5% + €0.50 | 5% + €0.50 | around 10% |
| Approval | Quick, self-serve | Application review, stricter | Instant |
| Hosted files | Yes | Yes | Yes |
| Tax-inclusive pricing toggle | Yes | Yes | Yes |
| Test mode | Yes | Yes | Yes (100%-off coupon / test purchase) |
| Best for | **This plugin** | Established companies, invoice-heavy B2B | Simplest possible start |

Rates above are the ones the vendors publish and they change — confirm on the pricing page before
you commit. Watch how the fee lands at **€15** rather than at a €50 price point: the fixed 50 cents
stops being rounding noise and becomes the bulk of the cost.

| On a €15 sale | Fee | Effective rate |
| --- | --- | --- |
| Lemon Squeezy / Paddle (5% + €0.50) | €1.25 | 8.3% |
| Gumroad (~10%) | €1.50 | 10% |

So the gap between them is about **€0.25** per unit, and roughly **€1.25** of every sale goes to the
store before withholding. At this price the fixed 50 cents is the part to negotiate away if you ever
outgrow self-serve — and it is the reason a €15 impulse price still needs a working checkout rather
than a manual invoice per buyer.

**Recommendation:** Lemon Squeezy. It is a real MoR, hosts the files, has a test mode, and lets you
keep the price tax-inclusive. Gumroad is the five-minute fallback if you want checkout live today;
Paddle is the better long-term home if you already run a company and want formal invoicing.

## 3. The product to create

| Field | Value |
| --- | --- |
| Name | `GoaSynth — Goa Trance Synthesizer (VST3)` |
| Type | Digital download, one-time payment, no subscription |
| Variants | None, or a single "Personal licence — 3 machines" |
| Price | **€15** in EUR |
| Prices include tax | **On** — so €15 is what everyone pays, everywhere |
| Generate licence keys | **Off** — the store's own key generator is useless here; use `GoaSynthKeygen` instead (see below) |
| Custom field | **"Machine ID (20 characters, from the plugin's activation screen)"** — **required** — see §3b |
| Delivery | Hosted file: the release ZIP from section 4, plus the serial you email per buyer |
| Refund window | 14 days — matches the site |
| Receipt + support email | A real support inbox, not a personal address |
| Tax category | Digital goods / software |
| Checkout questions | Email + country from the store, plus the machine ID field. Do not ask for DAW or OS: the plugin works on all three and you do not need the data |

**Leave the store's "generate licence keys" switch off.** GoaSynth does not check anything against the
store: activation is an RSA signature over a machine fingerprint, verified inside the plugin. A
generated key from the store would look like the real thing and activate nothing — the exact support
ticket you want to avoid. The serial you send is issued by your own keygen.

**The machine ID field is not optional.** It is what the serial is signed against. Most checkout forms
let you add a custom text field (Lemon Squeezy: *Checkout → custom fields*; Gumroad: a "custom field" on
the product; Paddle: custom checkout data). Mark it required, or you will be chasing buyers by email
before you can issue anything.

## 3b. Issuing serials — the fulfilment tool

Every serial is signed for one buyer's machine ID, so a command has to run per order. `GoaSynthFulfil`
does that in bulk straight from the store's order export, so a morning's sales are one command rather
than twenty minutes of copy-paste.

Build it next to the plugin (it is seller-side only, never shipped to buyers):

```bash
cmake --build build --config Release --target GoaSynthFulfil --parallel
# → build/GoaSynthFulfil_artefacts/Release/GoaSynthFulfil.exe
```

Export the orders from the store (Lemon Squeezy: *Orders → Export CSV*; Gumroad: *Sales → Export*) into
a folder, then:

```bash
GoaSynthFulfil --inbox C:\goasynth\orders --out C:\goasynth\fulfilled \
               --from "GoaSynth <orders@yourdomain>" --repo https://github.com/you/goasynth
```

Or leave it running and let it pick up each new export:

```bash
GoaSynthFulfil --inbox C:\goasynth\orders --watch --interval 60
```

One pass reads every `*.csv` and `*.json` in the inbox and writes:

| Path | What it is |
| --- | --- |
| `licenses/GoaSynth-<machineId>.goalicense` | the buyer-ready licence file, signed through the keygen's own core |
| `mail/<order>-<email>.eml` | a reply-ready message: buyer, subject, body and the licence attached — open it, check it, send it |
| `manifest.tsv` | every serial issued, keyed by order id: what makes re-runs safe |
| `needs-attention.tsv` | orders it refused to sign, with the reason |
| `activity.log` | append-only record of each pass and each revocation |

**It refuses to guess.** A column named `Machine ID` / `HWID` / `Device ID` (any case, or your custom
field's name) is used directly. With no such column it scans every value for a 20-hex token, and that
token must contain a letter — so a 20-digit order number can never be mistaken for a machine ID. If two
candidates are equally plausible the order goes to `needs-attention.tsv` and is *not* signed: a serial
bound to the wrong machine is a support ticket, and one sent a day late is not.

**Never issued twice.** The manifest is keyed by order id, so re-exporting, re-running after a crash, or
leaving `--watch` on all day cannot produce a second serial. The signature is deterministic anyway (the
same machine ID always yields the same serial) and the keygen's ledger keeps one line per distinct
serial. A licence file you delete by accident is simply re-created on the next pass.

**Refunds are handled, not just noticed.** When an export shows an order as refunded, the pass:

1. **revokes the serial** — it is taken out of the active ledger so it stops counting as a live licence;
2. **records why** — the serial, machine ID, reason (`refund - order 7001`) and timestamp go into
   `%APPDATA%\GoaSynth\Keygen\revoked_serials.txt`, because `--unregister` deletes the line and a refund
   deserves a trace;
3. **marks the order** in `manifest.tsv` (`revoked`, or `refunded` if it had never been issued) and writes
   the event to `activity.log`;
4. does nothing at all the second time round — revoking is idempotent.

`GoaSynthKeygen --list` then shows only live licences (plus a revoked count), and `--verify <serial>` on a
refunded one answers `VALID — … REVOKED on <date> - refund - order 7001`.

**A re-purchase puts the serial back.** If the same buyer buys again (or a corrected export shows the
order as paid), the next pass re-issues the serial — the signature is deterministic, so they get the
identical one back — and appends a `restored` event to `revoked_serials.txt`. `--list` and `--verify` then
treat it as a live licence again; `--verify` still tells the whole story (*was revoked on <date> — refund —
order 7001, later restored*), and the refund itself never leaves the log. Running the pass twice changes
nothing: a serial that is already live is not restored again. A licence file deleted by accident is
re-created from the manifest, never re-signed.

**The log is an append-only event stream**, one line per event (`revoked` or `restored`) with the serial,
machine ID, reason and timestamp; the newest line for a serial is its current state. It is also your
refund ledger: it answers "was order 7001 refunded?" six months later, which `--unregister` (a transfer,
where erasing the line is the point) cannot. The same operation by hand is
`GoaSynthKeygen --revoke <serial> [reason]`.

**A refund row that stays in your export never fights a re-purchase.** Store exports usually keep listing
an old order as refunded for ever, so if the same buyer later buys again (a new order key, same machine
ID) a naive pass would revoke the serial the new order holds — and since the new order is already
fulfilled, nothing would put it back. The tool checks for a newer paid order on the same machine and, when
there is one, leaves the serial live, marks the old order `refunded` in the manifest and says so
(*refund of 7001 left the serial live - it is held by order 7009*). Re-running changes nothing.

**What revocation does not do.** It cannot switch off the copy the buyer already installed: activation is
offline, so nothing reaches into their machine — the signature stays valid and the plugin keeps working.
Revocation is what stops that serial being a live licence you support or re-issue, and what gives you an
honest answer for a payment-provider dispute.

**Nothing to log in anywhere:** the keygen's ledger, `revoked_serials.txt` and `manifest.tsv` are your
fulfilment record, and `GoaSynthKeygen --list` prints every active serial with its machine ID and note.

**Try it risk-free:** `--dry-run` reports what would be issued and signs nothing; `--status` prints the
manifest and the current attention list.

**If a buyer pays without a machine ID** (the field was skipped, or they had not installed the plugin
yet): they appear in `needs-attention.tsv`. Send the download link and ask for the ID — or for them to
install it and press **COPY MACHINE ID** on the activation screen. The 24-hour trial covers the gap.
When the ID arrives, drop it into a small `manual.csv` in the inbox (`Order ID,Email,Product,Machine ID`)
and run the pass again; it is signed like any other order, and the .eml draft comes back with it.

**If a buyer changes computer or reinstalls Windows:** the machine ID changes and the old serial
correctly refuses to activate. Check their order with `--list`, then:

```bash
GoaSynthKeygen --unregister <serial>       # lift the old binding
GoaSynthKeygen --file <newMachineId> "Order #1234 — Jane (new PC)"
```

Issue the replacement free — the site says you will, and it is the difference between a customer and a
chargeback. Never send a serial that is not bound to the machine ID the buyer gave you: it will simply
fail, and they will assume the plugin is broken.

**One order by hand?** The keygen is still the right tool for the buyer who emails an ID at midnight —
and it takes the same `GOASYNTH_KEYGEN_DIR` override the fulfilment tool does:

```bash
GoaSynthKeygen --file <machineId> "Order #1234 - Jane"   # serial + a ready-to-send .goalicense
GoaSynthKeygen --genfile                                 # same, asked step by step
```

`--gen` issues the serial alone; `--file` wraps it in a `GOA-LICENSE-1` file the buyer double-clicks
(Windows, via the bundled `GoaSynthLicense.exe`) or **IMPORT**s on the activation screen. Send the file —
it is friendlier than a 41-character string, and it cannot be mistyped.

**Keep the keypair safe.** `%APPDATA%\GoaSynth\Keygen\` holds the RSA private key that signs every
serial. Lose it and you cannot issue another serial for any existing build — back it up somewhere you
also back up your tax records (and never commit it). The master key is the same story: it activates any
machine, so it belongs in a password manager, not in an email.

**If the master key leaks**, retire it with `GoaSynthKeygen --rotate-master` instead of deleting
`keys.txt`: that swaps the stretched digest while keeping the keypair, so no buyer has to be re-issued
and no activation stops working. It records the change in `master_rotations.txt`. Then rebuild and
re-upload the download — a copy that has already been downloaded keeps accepting the old key, because
the old digest is inside it, so the rebuild is the part that actually closes the leak.

Set **prices include tax** to on, and the `€15` on the site stays honest for an EU buyer too. If you
prefer to have VAT added on top, set `vatIncluded: false` in the site config so the pricing card
says "VAT added at checkout" instead of "VAT included".

### Description (paste-ready)

> **GoaSynth — a VST3 synthesizer built for Goa trance.**
>
> TB-303 squelch, seven-voice supersaws, hoover stabs, FM bleeps and PWM pads, with a tempo-locked
> trancegate and arp sequencer, two filters with serial/parallel/stereo-split routing, an 8-slot mod
> matrix, the full FX chain including OTT multiband squeeze, and an offline AI patch designer.
> 175 factory presets across nine families.
>
> One personal licence: three machines you use yourself, commercial releases included, no
> subscription, no account and no dongle — activation is an offline serial signed for your machine, sent
> by email after checkout. Try the full plugin free for 24 hours before you buy. Free updates for the
> whole 1.x line. 14-day refund.
>
> The AI patch designer works completely offline out of the box, and can optionally use your own Google
> Gemini or OpenAI API key (or a local Ollama/LM Studio server) for language-model generation — your key,
> your provider, your account.
>
> Source code is published under the AGPLv3 at <https://github.com/YOURUSER/goasynth> — buyers
> receive the corresponding source with their download.
>
> VST® is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.
> GoaSynth is an independent project, not affiliated with or endorsed by Steinberg.

That last paragraph matters: you are selling the binaries, the source is open, and the listing should
say so. Buyers who prefer to compile it themselves are welcome to.

## 4. The file to attach

Ship one ZIP per platform, or a single ZIP with all three — buyers tend to have one machine, but
multi-machine users appreciate the bundle.```
GoaSynth-1.0.0-win64.zip
  GoaSynth.vst3/            ← the whole bundle, copy it into your VST3 folder
  GoaSynthLicense.exe       ← optional: opens .goalicense files on double-click
  README.txt                ← install steps, activation steps, licence terms, source link
  EULA.txt
GoaSynth-1.0.0-macos.zip
GoaSynth-1.0.0-linux.zip
```

> **Build order matters.** `Source/LicenseKeys.h` is gitignored and generated by
> `GoaSynthKeygen --init`, and a fresh checkout builds with a **placeholder that rejects every serial**.
> Run `--init` once, rebuild, and verify activation *before* you package — a ZIP built from a clean clone
> without that step looks perfectly normal and silently refuses every serial you sell.

Build the artefacts with the commands in the main `README.md` (`GoaSynth_VST3` target, then take
`build/GoaSynth_artefacts/Release/VST3/GoaSynth.vst3`), copy the bundle whole — it is a folder, not a
file — and add a checksum file. Put the activation steps in `README.txt`: open the plugin, copy the
machine ID, send it with the order, then double-click the `.goalicense` file. Never ship a build you
have not opened in at least two DAWs.

## 5. Wire the URL into the site

Paste a handful of values into the `CONFIG` block at the top of `docs/app.js`:

```js
var CONFIG = {
  price: '€15',
  store: 'Lemon Squeezy',                                   // name used in the MoR sentence
  vatIncluded: true,                                        // or false if tax is added on top
  checkout: 'https://yourstore.lemonsqueezy.com/buy/1234abcd-…',   // ← the Buy button URL
  ordersEmail: 'orders@yourdomain',                         // email fallback / order questions
  support: 'support@yourdomain',                            // falls back to ordersEmail
  download: 'https://yourstore.lemonsqueezy.com/my-orders', // customer re-download page
  repo: 'https://github.com/YOURUSER/goasynth',
  releases: '',
  machineIdField: true      // checkout collects the buyer's machine id
};
```

`machineIdField` only controls one sentence on the pricing card — the note telling buyers that checkout
asks for their machine ID. Leave it `true` if you added the custom field in §3; set it `false` if you
take the ID over email instead, and the note hides itself rather than promising a field that isn't
there (the order-email fallback already asks for it automatically).

Where the URL comes from, per store:

- **Lemon Squeezy** — product → *Share* → copy the `https://<store>.lemonsqueezy.com/buy/<uuid>` link.
- **Gumroad** — product → *Share* → `https://<name>.gumroad.com/l/<slug>`.
- **Paddle** — the checkout link shown on the product; paste whatever it copies.
- **FastSpring** — the storefront or product URL.

The page then has three modes, and it only ever shows one:

| Config state | What visitors see |
| --- | --- |
| `checkout` set | Buy buttons open the store; the pricing card names the MoR and explains card/PayPal handling and VAT |
| `checkout` empty, `ordersEmail` set | Buy buttons become a pre-filled order email; the card explains orders are invoiced by email for now |
| Neither set | Buy buttons scroll to the pricing section; no payment method is promised |

That is deliberate: the site must never advertise a checkout that doesn't exist. Blocks tagged
`data-when="checkout" / "email" / "none"` in `index.html` switch accordingly, and `[data-price]` and
`[data-price-note]` are stamped from config, so a price change is a one-line edit.

## 6. Test before going live

1. Put the store in **test mode** (Lemon Squeezy: toggle test mode on the product; Gumroad: a 100%
   off coupon or test purchase).
2. Click **every Buy button** — header, hero, pricing card, footer, closing CTA — at desktop and at
   phone width, and confirm they all land on the store's checkout.
3. Buy with an **EU address** (e.g. Germany) and confirm the total is **€15**, not €15 plus VAT.
   This is the single most common pricing mistake.
4. Confirm the **receipt email** arrives, contains the download link, and that downloading works on a
   machine that has never seen your store account.
5. **Complete the licence loop end to end**, because this is the part that can fail after payment:
   - install the ZIP on a clean machine → the activation screen appears with a machine ID and the trial
     runs for 24 hours;
   - run `GoaSynthKeygen --file <thatId> "test"`, email the `.goalicense` file to yourself, and confirm
     double-click (or **IMPORT**) activates it;
   - confirm a serial signed for a *different* machine ID is refused with a readable message;
   - confirm `deactivate` then reactivates on the same machine.
6. Check the **refund path**: mark one test order refunded in a fresh export and re-run the pass. Confirm
   the serial disappears from `GoaSynthKeygen --list`, appears in `revoked_serials.txt` with the order
   number as its reason, and that the manifest row for that order now reads `revoked`.
7. Try the other two config modes by emptying `checkout` (and then `ordersEmail`) to make sure the
   copy swaps over cleanly.

## 7. Before you take real money

- **Policy pages.** Drafts already exist and are wired into the site footer — stores ask for these
  URLs at signup, and EU/UK consumer law expects a clear withdrawal/refund statement for digital
  goods:

  | Page | URL | What still needs doing |
  | --- | --- | --- |
  | Licence Agreement (EULA) | `/legal/eula.html` | Replace the `class="ph"` placeholders, then a lawyer's read of §§10 and 12 |
  | Terms of Sale | `/legal/terms.html` | Fill in the store name, VAT ID (if registered), and check §3 matches how the store handles tax. §6 promises the licence serial within one working day — make sure you can actually meet it |
  | Refund Policy | `/legal/refunds.html` | Must state the same 14 days as the site and the store setting. §5 says the serial is revoked with the refund, which `GoaSynthFulfil` now does for you — just make sure the export you feed it actually carries the refund |
  | Privacy Policy | `/legal/privacy.html` | Fill in the processors list, the retention periods, and your privacy contact. §2 already documents the machine ID, the licence files and the optional cloud AI engines — keep it that way if you change either |

  Every placeholder is wrapped in `<span class="ph">[…]</span>` and rendered in the accent colour, so
  an unfilled one is impossible to miss on a published page. Search for `class="ph"` in each file.
  These are templates written for this exact setup — AGPLv3 binaries sold under a personal licence —
  but they are not legal advice; have a lawyer read them, especially the liability cap, the governing
  law clause, and the interaction between the purchase licence and the AGPLv3.
- **Business identity.** Legal name and address for the store account and invoices; a tax ID/VAT
  number if you are registered (Germany also expects an Impressum on the site); bank details for
  payouts; and a payout schedule you are happy with.
- **Keep the site and the store identical.** Price, 14-day refund, three-machine seat, free 1.x
  updates, 24-hour trial, and the promise that a machine move is re-issued free. Any mismatch between
  the listing and the page is a refund request waiting to happen.
- **Back up the keypair** (`%APPDATA%\GoaSynth\Keygen\`) and the master key before your first sale, and
  keep a copy of every serial you issue (`--list`). Losing either is the one mistake with no recovery.
- **AGPL consistency.** The listing must not describe the plugin as closed source. The receipt, the
  README in the ZIP and the product description should all carry the repository link — and the listing
  should say plainly that the build is activated with a serial, since buyers who read the AGPL will ask
  about it anyway. The EULA §2 and §7 and the Privacy Policy §2 explain exactly how the two fit
  together.
