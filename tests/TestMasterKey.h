#pragma once

// Throwaway master key used by the test suite ONLY.
//
// The test build compiles Source/License.cpp with GOA_TEST_BUILD, which swaps
// GOA_MASTER_DIGEST for the digest below (see Source/License.cpp). That means
// no test ever has to carry the seller's real master key, and nothing in this
// repository is a working unlock for a shipped build: the production plugin
// verifies against the digest in Source/LicenseKeys.h, which this never matches.
//
// Regenerate a fresh pair any time with the keygen's digest routine:
//   digest = sha256("goa-master::" + KEY), then 64 more sha256 rounds.
#define GOA_TEST_MASTER_KEY    "GoaSynthTestMaster!23"
#define GOA_TEST_MASTER_DIGEST "85769D35D34CD25FE2B8AEE476482D8BE857D67BDBF5EEABE7F87BF6F2049A46"
