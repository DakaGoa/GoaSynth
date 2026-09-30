#!/usr/bin/env python3
"""One-shot fill of the legal-page placeholders with the seller's real details.

Identity facts confirmed by the seller (September 2026):

    David Jeremic — Cetinjska 26, 11080 Beograd (Zemun), Serbia
    orders/support/privacy @ goasynth.*.gmail.com
    Merchant of record: Lemon Squeezy. Not VAT-registered. Refunds: 14 days.

Run once, verify, then delete or keep as the record of what was filled:

    python tools/fill-legal.py
"""
import re
import sys
from pathlib import Path

LEGAL = Path(__file__).resolve().parent.parent / "docs" / "legal"

NAME    = "David Jeremic"
STREET  = "Cetinjska 26"
CITY    = "11080 Beograd (Zemun)"
COUNTRY = "Serbia"
ADDRESS = f"{NAME}, {STREET}, {CITY}, {COUNTRY}"
ORDERS  = "goasynth.orders@gmail.com"
SUPPORT = "goasynth.support@gmail.com"
PRIVACY = "goasynth.privacy@gmail.com"
STORE   = "Lemon Squeezy"
REPO    = "https://github.com/Y4m4/GoaSynth"
DATE    = "30 September 2026"

# Stale template instruction blocks (first HTML comment in each file) become
# a record of the fill instead of an instruction that no longer applies.
FILLED_COMMENT = """
  ============================================================================
  Filled {date} with the seller's details: {name}, {street}, {city}, {country}.
  Merchant of record: {store}. Not a substitute for legal review — have a
  lawyer check §3 (price and taxes), §7 (withdrawal) and the complaints
  section before you rely on them.
  ============================================================================
"""

# Replacements applied to every page, longest first so the compound address
# span wins over its pieces.
COMMON = [
    (r"\[your legal name, street, postcode, city, country\]", ADDRESS),
    (r"\[your legal name\]", NAME),
    (r"\[store name, e\.g\. Lemon Squeezy\]", STORE),
    (r"\[store name\]", STORE),
    (r"\[support\@yourdomain\]", SUPPORT),
    (r"\[orders\@yourdomain\]", ORDERS),
    (r"\[privacy\@yourdomain\]", PRIVACY),
    (r"\[street and number\]", STREET),
    (r"\[postcode and city\]", CITY),
    (r"\[your country\]", COUNTRY),
    (r"\[country\]", COUNTRY),
    (r"\[your city\]", "Belgrade"),
    (r"\[date\]", DATE),
    (r"\[repository URL\]", REPO),
    (r"\[hosting provider\]", "GitHub Pages (GitHub, Inc.)"),
    (r"\[email provider\]", "Gmail (Google)"),
]

# Per-file specifics: Serbia's accounting/tax retention is 10 years; the
# support-mailbox window follows the template's own suggestion.
PER_FILE = {
    "privacy.html": [
        (r"\[the statutory period in your country, e\.g\. 6–10 years\]",
         "10 years (Serbian accounting and tax law)"),
        (r"\[e\.g\. 24 months\]", "24 months"),
        (r"\[e\.g\. 14 days\]", "14 days"),
    ],
    # Terms: the seller is not VAT-registered, so the VAT-ID sentence goes;
    # the tax note is resolved (Lemon Squeezy prices are tax-inclusive, as
    # the surrounding paragraph already states). Applied after the unwrap,
    # so these match bare text.
    "terms.html": [
        (r"\s*\[If you are registered for VAT: your VAT ID is\s*\[VAT ID\]\.\]", ""),
        (r"\s*\[If your store adds tax on top instead, rewrite this paragraph to say so\.\]", ""),
    ],
}


def fill(path: Path) -> int:
    text = path.read_text(encoding="utf-8")

    # Retire the template comment (first comment block only).
    text = re.sub(r"<!--.*?-->", FILLED_COMMENT.format(
        date=DATE, name=NAME, street=STREET, city=CITY, country=COUNTRY,
        store=STORE).strip(), text, count=1, flags=re.S)

    # Unwrap the highlight spans first, keeping their placeholder text, so
    # the replacements below leave no class="ph" wrapper behind.
    text = re.sub(r'<span class="ph">(.*?)</span>', r"\1", text, flags=re.S)

    count = 0
    for pattern, replacement in COMMON + PER_FILE.get(path.name, []):
        text, n = re.subn(pattern, replacement, text, flags=re.S)
        count += n

    path.write_text(text, encoding="utf-8", newline="\n")
    return count


def main() -> int:
    total = 0
    ok = True
    for name in ("eula.html", "terms.html", "refunds.html", "privacy.html"):
        n = fill(LEGAL / name)
        body = (LEGAL / name).read_text(encoding="utf-8")
        leftover = body.count('class="ph"')
        print(f"  {name:14} {n:2} replacements, {leftover} placeholders left")
        total += n
        if leftover:
            ok = False
        # No bracketed placeholders may survive anywhere either.
        for m in re.finditer(r"\[[^\]<>]{2,60}\]", body):
            print(f"    LEFTOVER in {name}:", m.group(0)[:90])
            ok = False

    print(f"done: {total} replacements, " + ("clean" if ok else "LEFTOVERS REMAIN"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
