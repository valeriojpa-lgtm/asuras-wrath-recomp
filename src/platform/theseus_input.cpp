#include "platform/theseus_input.h"

// T05 intentionally keeps NativeInputPolicy free of platform/runtime code.
// Physical device drivers and the guest XAM ABI remain behind a temporary
// compatibility adapter while policy/configuration belong to Theseus.
