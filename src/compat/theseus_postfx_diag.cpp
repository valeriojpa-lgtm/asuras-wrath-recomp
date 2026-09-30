#include <cstdlib>

bool TheseusPostFxPatchSiteEnabled(int site) noexcept {
  static const int mask = []() noexcept {
    const char* value = std::getenv("THESEUS_POSTFX_MASK");
    if (!value || !*value) {
      return 0;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 0);
    if (end == value) {
      return 0;
    }
    return static_cast<int>(parsed) & 0x1F;
  }();

  if (site < 0 || site >= 5) {
    return false;
  }
  return (mask & (1 << site)) != 0;
}
