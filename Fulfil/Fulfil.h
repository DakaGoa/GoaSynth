#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
// GoaSynth order fulfilment (seller-side, never shipped).
//
// Reads the store's order exports (CSV or JSON) from an inbox folder, finds
// each buyer's 20-char MACHINE ID, signs a licence serial for it through the
// shared KeygenCore (same code path the plugin verifies), writes the buyer's
// .goalicense file and a reply-ready .eml with that file attached, and records
// what it did in a manifest so a re-run can never issue twice.
//
// See CHECKOUT.md (beside this file) §3b for the workflow it replaces.
namespace fulfil
{
struct Options
{
    juce::File inbox;                 // where the store's exports land
    juce::File outbox;                // defaults to <inbox>/../fulfilled
    juce::File bodyTemplate;          // optional: file overriding the email body
    juce::String fromAddress = "GoaSynth orders <orders@example.com>";
    juce::String subject = "Your GoaSynth licence";
    juce::String productFilter = "goasynth";   // skip other products in the same export
    juce::String repoUrl;             // AGPL source link, mentioned when set
    int intervalSeconds = 30;         // --watch poll interval
    bool watch = false;
    bool dryRun = false;
    bool quiet = false;
};

struct Summary
{
    int filesScanned = 0;
    int ordersSeen = 0;
    int issued = 0;
    int moved = 0;                    // machine moves: old serial retired, new one issued
    int revoked = 0;                  // refunds pulled out of the active ledger
    int alreadyFulfilled = 0;
    int skipped = 0;                  // refunded, or a different product
    int needAttention = 0;            // no usable machine id
    juce::StringArray notes;
    juce::String error;               // non-empty when the pass could not run
};

// <inbox>/../fulfilled — deliberately outside the inbox so the tool never
// re-reads its own output.
juce::File defaultOutbox (const juce::File& inbox);

// One pass over the inbox. Safe to call repeatedly.
Summary runOnce (const Options&);

// Console entry point (see GoaSynthFulfil.cpp for main()).
int run (int argc, char* argv[]);
}
