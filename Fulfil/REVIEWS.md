# Reviews — asking buyers, and publishing what they say

Seller-side notes. This file is **not** served: everything under `docs/` is the website, and reviews
only reach the site through `tools/make-reviews.py` once a buyer has agreed to be quoted.

Why bother: `aggregateRating` on the `SoftwareApplication` is the one piece of structured data still
worth having and still out of reach — it needs a rating average and a count, and both have to come
from real people. (FAQ markup is gone; see the README. Software app review snippets are still
supported, and unlike a LocalBusiness reviewing itself, a software vendor rating its own product is
explicitly eligible.)

---

## 1. What Google requires

Quoted from [Review snippet structured data](https://developers.google.com/search/docs/appearance/structured-data/review-snippet)
(checked 30 September 2026), because these are the rules that decide whether any of this works:

- **The reviews must be visible on the page that carries the markup.** "Make sure the review content
  you mark up are readily available to users from the marked-up page... If you use AggregateRating,
  users should be able to see that aggregate rating on the page." That is why the generated section
  shows the average and the count as text as well as in the JSON-LD.
- **Don't aggregate reviews or ratings from other websites.** So a Trustpilot or G2 widget cannot be
  marked up here, and their scores cannot be folded into our number. Reviews are captured directly.
- **Don't include fake or undisclosed incentivized reviews** — added 24 July 2026. A review written in
  exchange for "money, discounts, vouchers, or free products" must "clearly and prominently disclose"
  it. A free review copy is fine; a free review copy without a line saying so is a manual action
  waiting to happen. Record it in the review's `incentive` field and the tool prints it.
- **Only mark up reviews you have.** If there is no aggregate, the markup must not contain one —
  inventing stars is the fastest way to lose every rich result on the domain.
- An `author` name must be a real name under 100 characters (the tool enforces the limit), and each
  published review carries a date.
- Google recommends only accepting ratings that come with a comment and a name, which is why the
  store below has no field for a bare star rating.

## 2. Asking

Ask **once, ~7–10 days after purchase** — long enough that they have made something with it, before
the goodwill of the sale has faded. Two other moments are worth a sentence each: when a support
question has just been solved well, and when someone emails to say they like it (ask then and there,
while they are typing about it).

Never ask a second time after a no, and never make the ask a condition of support.

The template, kept short on purpose:

> Subject: How's GoaSynth treating you?
>
> Hi {name} — you bought GoaSynth {n} days ago, so I wanted to check in: is anything annoying you, or
> anything you can't work out? Reply to this and it's me, not a ticket queue.
>
> And if you have a minute: would you leave a line or two about it that I could quote on the site,
> with your first name and initial? Star rating out of five if you want to give one. Happy to leave
> it out if you'd rather not, and a critical one is genuinely more useful to me than a polite one.
>
> Either way, thanks for buying it.
>
> {seller}

What "agreed" means, and why the store records it: a review is published **only** when the buyer says
so, in a reply we still have. That reply is the `consent` flag, and a buyer can withdraw it later —
which is what `consent: false` is for, and why the tool removes the whole section when every review
is withdrawn.

**A bad review is still a review.** Publish it. An average built from the reviews you were willing to
show is a claim about a product, and the ones you hid are exactly what the average is
misrepresenting — Google's "don't include fake reviews" rule covers the omission as much as the
invention, and the first refund demand that cites "your site says 5 stars" costs more than the star.
If a review is unfair or wrong, reply to it publicly in the section copy rather than dropping it.

## 3. The store

`Fulfil/reviews.json` — **gitignored**, because it holds the order reference and the buyer's email
address, and this repository is public. It is the only place reviews live until they are published.

```json
{
  "reviews": [
    {
      "name": "Marek K.",
      "rating": 5,
      "date": "2026-10-14",
      "text": "Wrote the whole bassline on the train. The gate shape cell is the thing I didn't know I wanted.",
      "consent": true,
      "incentive": "",
      "order": "LS-1042",
      "email": "buyer@example.com"
    }
  ]
}
```

| field | what it is |
| --- | --- |
| `name` | how they want to be credited: first name and initial is the usual. Under 100 characters. |
| `rating` | whole number 1–5. Omit the review entirely rather than guessing one. |
| `date` | ISO `YYYY-MM-DD` — the day they sent it, not the day you publish it. |
| `text` | their words, trimmed. Light typo fixing is fine; don't rewrite the opinion. |
| `consent` | `true` only when the buyer agreed to be quoted. `false` keeps it out of the site. |
| `incentive` | disclose a free copy, discount or other benefit here, in the words the reader sees. Empty when there was none. |
| `order`, `email` | private bookkeeping. Never published — the tool has no code path that reads them. |

## 4. Publishing

```bash
python tools/make-reviews.py            # writes the section and the aggregateRating into docs/index.html
python tools/make-reviews.py --check    # fails if they are out of date
```

Then commit `docs/index.html`, push, and the deploy carries it. The tool refuses rather than guesses:
a rating outside 1–5, a missing name, text or date, an over-long name, a date that is not ISO, a
review marked `consent` with no name — all stop the run with the reason. With no store file at all it
does nothing, and with a store that yields no publishable reviews it **removes** the section and the
aggregate rating, which is what withdrawing consent has to look like.

The generated section carries the reviews, the average and the count, and `DocsCheck` fails the suite
if the three disagree — so a hand-edited star rating cannot ship. Ratings are computed as a whole
number of tenths with halves rounded up, on both sides, so the two never disagree about `4.65`.

## 5. When to stop

Two or three reviews with real text are worth more than a dozen that all read "great plugin!". If the
section is going to say "4.9 from 3 buyers", leave it off instead: a thin rating invites exactly the
scepticism it was meant to answer.
