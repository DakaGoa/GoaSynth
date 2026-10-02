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

## 0. The whole thing, in order

Everything in this file, as a list to work through. Nothing here needs a server: the store hosts the
checkout *and* the download, and the plugin activates offline.

| # | Do this | Where |
| --- | --- | --- |
| 1 | Choose the merchant of record | §1, §2 |
| 2 | Open the store account: business identity, payout details, receipt and support addresses | §3, §7 |
| 3 | Create the product: €15 one-time, tax-inclusive, **licence keys off**, a **required Machine ID** custom field, 14-day refund | §3 |
| 4 | Attach the packaged build as the product's file, and enable the customer library so buyers can re-download | §4 |
| 5 | Copy the product's checkout URL into `CONFIG.checkout` | §5 |
| 6 | Copy the customer-library URL into `CONFIG.download` | §5 |
| 7 | Build the seller-side tools once — `GoaSynthKeygen --init`, then `GoaSynthFulfil` | §3b |
| 8 | Test in the store's test mode: buy, receive, activate, refund | §6 |
| 9 | Push, then prove the deploy is live | §6 |
| 10 | Before the first real sale: policy pages, keypair backup, and a listing that matches the site | §7 |

Steps 2–4 are account forms at the store, and are the only part that cannot be done from this
repository. Everything else already exists here: **steps 5 and 6 are the two-line edit** that turns
the site's "checkout is being wired up" into a working Buy button, and no other page has to change.

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
| Receipt + support email | `goasynth.orders@gmail.com` for receipts, `goasynth.support@gmail.com` for support — the addresses the legal pages already publish |
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

The replies for the questions that actually arrive — a buyer with no machine ID, a machine move, a lost
licence file, a refund, a serial that "won't activate" — are written out in
[SUPPORT.md](SUPPORT.md), each one grounded in what these tools really do.

Build it next to the plugin (it is seller-side only, never shipped to buyers):

```bash
cmake --build build --config Release --target GoaSynthFulfil --parallel
# → build/GoaSynthFulfil_artefacts/Release/GoaSynthFulfil.exe
```

Export the orders from the store (Lemon Squeezy: *Orders → Export CSV*; Gumroad: *Sales → Export*) into
a folder, then:

```bash
GoaSynthFulfil --inbox C:\goasynth\orders --out C:\goasynth\fulfilled \
               --from "GoaSynth <goasynth.orders@gmail.com>" --repo https://github.com/Y4m4/GoaSynth
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
| `manifest.tsv` | every order's state, keyed by order id: `issued`, `moved`, `refunded` or `revoked` — what makes re-runs safe |
| `needs-attention.tsv` | orders it refused to sign, with the reason |
| `activity.log` | append-only record of each pass and each revocation |

**Store export shapes it understands.** A CSV is read by its header row, so column order does not matter
and a real Lemon Squeezy export can carry both `Product Name` and `Product ID` — the id is ignored, never
mistaken for the product. A JSON export is read whether it is a bare array of orders or a wrapper object
(`{"orders":[…]}` / `{"data":[…]}`), and separators do not matter: `machine_id`, `Machine ID` and
`machineid` all mean the same column, so both `snake_case` and spaced headers work.

**Mind the product filter.** The pass fulfils an order only when its product value contains `--product`
(default `goasynth`). That is a substring match, so a *different* product whose name also contains
"goasynth" (a bundle, a patch pack) would be pulled in. If the store sells more than the synth, pass the
listing's own name instead, e.g. `--product "GoaSynth — Goa Trance Synthesizer"`.

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

`GoaSynthKeygen --list` then shows only live licences (plus revoked and moved counts, counted separately),
and `--verify <serial>` on a refunded one answers `VALID — … REVOKED on <date> - refund - order 7001`. A
serial retired by a machine move answers `MOVED` instead, so a move never reads as a refund.

**A re-purchase puts the serial back.** If the same buyer buys again (or a corrected export shows the
order as paid), the next pass re-issues the serial — the signature is deterministic, so they get the
identical one back — and appends a `restored` event to `revoked_serials.txt`. `--list` and `--verify` then
treat it as a live licence again; `--verify` still tells the whole story (*was revoked on <date> — refund —
order 7001, later restored*), and the refund itself never leaves the log. Running the pass twice changes
nothing: a serial that is already live is not restored again. A licence file deleted by accident is
re-created from the manifest, never re-signed.

**The log is an append-only event stream**, one line per event (`revoked`, `restored` or `moved`) with the
serial, machine ID, reason and timestamp; the newest line for a serial is its current state. It is also your
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

If the buyer's machine has changed since, reuse their **original Order ID** in `manual.csv` and the pass
does the move for you: seeing the same order against a different machine id, it retires the old serial and
issues for the new one in a single step (the same operation as `--reissue` below) instead of silently
treating the order as already fulfilled. The order's newest `manifest.tsv` row then reads `moved` — not
`issued` — so `--status` counts it as a moved seat rather than a live licence.

**If a buyer changes computer or reinstalls Windows:** the machine ID changes and the old serial
correctly refuses to activate. Check their order with `--list`, then:

```bash
GoaSynthKeygen --reissue <serial> <newMachineId> "Order #1234 — Jane (new PC)"
```

`--reissue` is the old `--unregister` + `--file` pair as one step: it retires the old serial, records the
move as a **`moved` event** (reason `machine move`) in `revoked_serials.txt` — distinct from a refund, so
`--verify` and `--list` never call a move a revocation — and writes the buyer-ready
`GoaSynth-<newMachineId>.goalicense` in the same pass. One command, no half-finished move.

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
> Source code is published under the AGPLv3 at <https://github.com/Y4m4/GoaSynth> — buyers
> receive the corresponding source with their download.
>
> VST® is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.
> GoaSynth is an independent project, not affiliated with or endorsed by Steinberg.

That last paragraph matters: you are selling the binaries, the source is open, and the listing should
say so. Buyers who prefer to compile it themselves are welcome to.

## 4. The file to attach

The packaging already exists: `tools/make-release.py` turns the built installer into the ZIP a buyer
gets, and publishes the hashes for it in the same pass.

```bash
python tools/make-release.py           # build, package, publish the checksums
python tools/make-release.py --check   # report what would change, change nothing
```

It writes `dist/GoaSynth-<version>-win64.zip`:

```
GoaSynth-1.1.0-win64.zip
  GoaSynth-Setup.exe        ← run this: installs the plugin, the standalone and the licence helper
  GoaSynthLicense.exe       ← opens .goalicense files on double-click
  README.txt                ← install steps, activation steps, the published checksums, source link
  EULA.txt                  ← the licence terms, taken from docs/legal/eula.html
```

The plugin bundle lives *inside* the installer rather than beside it in the ZIP, which is why the
release block on the site publishes three hashes: the ZIP, the `GoaSynth-Setup.exe` inside it, and
the standalone `GoaSynth.exe` the installer puts next to the plugin.

Two more things go to the site in the same command, and both are meant to be public:

- `docs/downloads/SHA256SUMS.txt` — so a buyer can check what they downloaded against a file the
  seller cannot quietly rewrite;
- the release block in `docs/index.html` — the version, the commit it was built from, and the hashes.

**Attach that ZIP to the store product** (§3) and turn the store's customer library on, so the
receipt carries a download link buyers can return to. That is the whole delivery path: the store
hosts the file, the serial arrives by email from §3b, and no binary is ever served from the
repository.

> **Build order matters.** `Source/LicenseKeys.h` is gitignored and generated by
> `GoaSynthKeygen --init`, and a fresh checkout builds with a **placeholder that rejects every serial**.
> Run `--init` once, rebuild, and verify activation *before* you package — a ZIP built from a clean clone
> without that step looks perfectly normal and silently refuses every serial you sell.

Do not hand-build that ZIP. `make-release.py` refuses a **dirty working tree**, a Setup installer
**older than the newest file under `Source/`** (which would ship last week's code with a fresh hash),
and a missing installer, plugin bundle or licence helper — the three ways a release goes out wrong. It
builds the targets itself, so the two commands above are the whole release, and the hashes on the site
stop being a claim you made by hand.

The activation steps it puts in `README.txt` are the ones that matter: open the plugin, copy the
machine ID, send it with the order, then double-click the `.goalicense` file.

**Only Windows is packaged today.** The site's hero line advertises **VST3 + AU · Windows · macOS ·
Linux**, so either package the other two platforms (a macOS build needs a Mac with the SDK, and the AU
wrapper has its own target) or reword that line before taking money: a macOS buyer who pays and
receives a Windows installer is a refund, not a customer. Never ship a build you have not opened in at
least two DAWs.

## 5. Wire the URL into the site

The `CONFIG` block at the top of `docs/app.js` is the only place the store touches the site. Its
shape, with the placeholders spelled out:

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
  machineIdField: true      // checkout collects the buyer's machine id
};
```

**What the repository has today.** Three values are already set, and three are deliberate blanks that
the store fills in:

```js
  store: 'Lemon Squeezy',                       // set — the legal pages already name it
  vatIncluded: true,                            // set — keep in step with the store's tax setting
  repo: 'https://github.com/Y4m4/GoaSynth',     // set — see the note below on why this is written down
  checkout: '',                                 // ← paste the product's checkout URL: Buy goes live
  download: '',                                 // ← paste the customer-library URL
  support: '',                                  // ← paste goasynth.support@gmail.com
```

That is the whole wiring: no page has to be edited, and no build has to be re-run — the site is
static, and `CONFIG` is read at load time. `tools/check-deployed.py` will confirm afterwards that the
live copy is this copy.

| Value | Where it comes from | When it is empty or wrong |
| --- | --- | --- |
| `checkout` | product → *Share* → the `https://<store>.lemonsqueezy.com/buy/<uuid>` link | every Buy button scrolls to the pricing card and the page says the checkout is being wired up — the honest state, and the one it is in now |
| `download` | the store's customer/library page, the one a receipt links to | the Install buttons fall back to the install steps on this page, which say the link arrives with the receipt |
| `support` | `goasynth.support@gmail.com`, the address the legal pages publish | the "ask first" sentence is removed rather than pointing nowhere |
| `ordersEmail` | your order inbox — **only if you intend to take orders by email** | setting it flips the whole page into email-order mode: Buy buttons become a pre-filled mail, and the site says orders are taken by email for now. That is a promise to invoice, sign and reply by hand, so leave it empty until you mean it |

Three notes, each of which has already cost time here once:

- **`download` is never derived from the repository.** The paid build is not published in a public
  repository, so a fallback to `<repo>/releases/latest` points buyers at an empty page — and this site
  did exactly that, invisibly, because an empty release list answers 404 rather than saying so. An
  empty `download` is a working state; a derived one was not.
- **Leave `repo` written down.** It has a fallback that derives the repository from a `*.github.io`
  hostname, so a fork points at itself, but that only works while the site is hosted on `github.io`:
  on a custom domain every link that depends on it goes empty. One line removes the dependency on where
  the site is hosted. (The fallback itself was also broken until recently — it compared the last ten
  characters of the hostname, dot included, against `github.io` — which is why the Source and
  Repository links were hidden on the deployed site.)
- **A fragment is not a download URL.** `CONFIG.download` is also what the "copy link" button copies,
  so putting `#install` in it would copy the string `#install` and call that a download link. Leave it
  empty and let the links fall back.

`machineIdField` only controls one sentence on the pricing card — the note telling buyers that checkout
asks for their machine ID. Leave it `true` if you added the custom field in §3; set it `false` if you
take the ID over email instead, and the note hides itself rather than promising a field that isn't
there (the order-email fallback already asks for it automatically).

If you use a different store, the link comes from:

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
8. **Push, then prove the deploy landed.** The store's test purchase is not the live site; the live
   site is whatever GitHub Pages is serving, which is not always the commit you just pushed:

   ```bash
   python tools/check-deployed.py --wait 60
   ```

   It fetches every file in `docs/` and compares it with the repository byte for byte, checks each
   page's canonical link and `og:url` against the address it was actually served at, follows the
   sitemap's `<loc>` list, rechecks the verification token, and confirms the one-address redirect. A
   push that has not been published yet looks exactly like drift, which is what `--wait` is for.
9. **Re-check the claims the store can contradict.** `DocsCheck` compares `CONFIG.price` with every
   euro amount on the pages, so a store price that is not €15 is a red build rather than a refund
   request. The refund window and the three-machine seat are only compared by eye, so make the listing
   say what the pages say: 14 days, three machines you use yourself.

## 7. Before you take real money

- **Policy pages.** Drafts already exist and are wired into the site footer — stores ask for these
  URLs at signup, and EU/UK consumer law expects a clear withdrawal/refund statement for digital
  goods:

  | Page | URL | What still needs doing |
  | --- | --- | --- |
  | Licence Agreement (EULA) | `/legal/eula.html` | Nothing mechanical — the seller details are in. A lawyer's read of §§10 and 12 (liability, termination) is still owed before the first sale |
  | Terms of Sale | `/legal/terms.html` | Check §3 against how the store actually handles tax, and that §6's "serial within one working day" is a promise you can keep — `GoaSynthFulfil` makes it easy, but it needs running |
  | Refund Policy | `/legal/refunds.html` | Must keep matching the store setting: 14 days. §5 says the serial is revoked with the refund, which `GoaSynthFulfil` does — provided the export you feed it carries the refund |
  | Privacy Policy | `/legal/privacy.html` | Keep the processor list, the retention periods and the privacy contact in step with the store you actually sign up with |

  The placeholders are resolved: the seller is named (David Jeremic, Cetinjska 26, 11080 Beograd,
  Serbia), the three addresses are real, and the merchant of record is named in all four pages. If
  `grep -n 'class="ph"' docs/legal/*.html` ever matches again, something has been un-filled — the
  styled placeholders were the whole point of that class.

  These are templates written for this exact setup — AGPLv3 binaries sold under a personal licence —
  but they are not legal advice; have a lawyer read them, especially the liability cap, the governing
  law clause, and the interaction between the purchase licence and the AGPLv3.
- **Business identity.** The store account needs what the legal pages already state: David Jeremic,
  Cetinjska 26, 11080 Beograd (Zemun), Serbia — plus payout details and a payout schedule you are
  happy with. A Serbian seller who is **not VAT-registered** is exactly the case a merchant of record
  exists for: the store charges and remits the VAT. If that ever changes, the legal pages have to say
  so and `CONFIG.vatIncluded` has to follow the store.
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
