#pragma once

// Console key generator for GoaSynth (seller-side tool).
//   GoaSynthKeygen --init                 create a fresh keypair + master key
//   GoaSynthKeygen --gen <machineId>      issue a serial for that machine id
//   GoaSynthKeygen --list                 show issued serials + machine ids
//   GoaSynthKeygen --verify <serial>      check a serial against the keypair
#include <juce_core/juce_core.h>

namespace keygen
{
int run (int argc, char* argv[]);
}
