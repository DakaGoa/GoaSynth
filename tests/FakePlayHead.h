// Test double for a playing host transport: feeds a fixed ppq position so the
// host-synced clocks (arp / trancegate / pump) run deterministically in the
// suite.
struct FakePlayHead : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        ++calls;
        PositionInfo pi;
        pi.setIsPlaying (true);
        pi.setBpm (120.0);
        pi.setPpqPosition (ppq);
        pi.setTimeInSeconds (ppq * 0.5);
        return pi;
    }

    double ppq = 0.0;   // the test advances this per block
    mutable int calls = 0;   // how many times the processor consulted us
};
