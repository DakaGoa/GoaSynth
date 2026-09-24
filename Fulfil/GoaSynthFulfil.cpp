#include "Fulfil.h"

//==============================================================================
// GoaSynth order fulfilment (seller-side console tool). Never shipped to
// buyers: it needs the RSA private key that only the seller holds.
int main (int argc, char* argv[])
{
    return fulfil::run (argc, argv);
}
