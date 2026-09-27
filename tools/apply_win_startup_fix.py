from pathlib import Path

path = Path("tools/rexglue/src/core/filesystem_win.cpp")
text = path.read_text(encoding="utf-8")

old = """std::filesystem::path GetExecutablePath() {
  wchar_t* path;
  auto error = _get_wpgmptr(&path);
  return !error ? std::filesystem::path(path) : std::filesystem::path();
}"""

new = """std::filesystem::path GetExecutablePath() {
  std::wstring path(32768, L'\\0');
  DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) {
    return std::filesystem::path();
  }
  path.resize(length);
  return std::filesystem::path(path);
}"""

if old not in text:
    raise SystemExit("Expected GetExecutablePath block not found")

path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("Windows startup patch applied successfully")
