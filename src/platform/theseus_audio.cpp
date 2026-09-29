#include "platform/theseus_audio.h"

// T06 keeps the stable host-facing audio policy independent from ReXGlue.
// XMA decoding, Xbox audio ABI translation and SDL sample submission remain a
// temporary compatibility bridge until later audio milestones.
