#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include "mspack.h"
#include "lzx.h"
}

struct MemoryFile {
  mspack_system sys;
  std::vector<uint8_t>* buffer;
  long long offset;
};

static mspack_file* mem_open(mspack_system*, const char*, int) { return nullptr; }
static void mem_close(mspack_file*) {}

static int mem_read(mspack_file* file, void* buffer, int chars) {
  auto* f = reinterpret_cast<MemoryFile*>(file);
  const auto remaining = static_cast<long long>(f->buffer->size()) - f->offset;
  const int total = static_cast<int>(std::min<long long>(chars, std::max<long long>(0, remaining)));
  if (total > 0) {
    std::memcpy(buffer, f->buffer->data() + f->offset, total);
    f->offset += total;
  }
  return total;
}

static int mem_write(mspack_file* file, void* buffer, int chars) {
  auto* f = reinterpret_cast<MemoryFile*>(file);
  const auto remaining = static_cast<long long>(f->buffer->size()) - f->offset;
  const int total = static_cast<int>(std::min<long long>(chars, std::max<long long>(0, remaining)));
  if (total > 0) {
    std::memcpy(f->buffer->data() + f->offset, buffer, total);
    f->offset += total;
  }
  return total;
}

static int mem_seek(mspack_file* file, off_t offset, int mode) {
  auto* f = reinterpret_cast<MemoryFile*>(file);
  long long next = 0;
  if (mode == MSPACK_SYS_SEEK_START) next = offset;
  else if (mode == MSPACK_SYS_SEEK_CUR) next = f->offset + offset;
  else if (mode == MSPACK_SYS_SEEK_END) next = static_cast<long long>(f->buffer->size()) + offset;
  else return -1;
  if (next < 0 || next > static_cast<long long>(f->buffer->size())) return -1;
  f->offset = next;
  return 0;
}

static off_t mem_tell(mspack_file* file) {
  return static_cast<off_t>(reinterpret_cast<MemoryFile*>(file)->offset);
}

static void mem_message(mspack_file*, const char*, ...) {}
static void* mem_alloc(mspack_system*, size_t bytes) { return std::calloc(bytes, 1); }
static void mem_free(void* ptr) { std::free(ptr); }
static void mem_copy(void* src, void* dst, size_t bytes) { std::memcpy(dst, src, bytes); }

static std::vector<uint8_t> ReadAll(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + path);
  f.seekg(0, std::ios::end);
  const auto size = f.tellg();
  f.seekg(0, std::ios::beg);
  if (size < 0) throw std::runtime_error("cannot size " + path);
  std::vector<uint8_t> v(static_cast<size_t>(size));
  if (!v.empty()) f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(v.size()));
  if (!f && !v.empty()) throw std::runtime_error("cannot read " + path);
  return v;
}

static void WriteAll(const std::string& path, const std::vector<uint8_t>& v) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) throw std::runtime_error("cannot create " + path);
  if (!v.empty()) f.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size()));
  if (!f) throw std::runtime_error("cannot write " + path);
}

int main(int argc, char** argv) {
  if (argc != 6) {
    std::cerr << "usage: asura_lzx_ref <compressed.bin> <output_len> <window_size> <reference.bin> <output.bin>\n";
    return 2;
  }

  try {
    auto input = ReadAll(argv[1]);
    const size_t output_len = std::stoull(argv[2], nullptr, 0);
    const uint32_t window_size = static_cast<uint32_t>(std::stoul(argv[3], nullptr, 0));
    auto reference = ReadAll(argv[4]);

    if (!window_size || (window_size & (window_size - 1)) != 0) {
      throw std::runtime_error("window_size must be a power of two");
    }
    if (reference.size() > window_size) {
      throw std::runtime_error("reference larger than LZX window");
    }

    unsigned window_bits = 0;
    for (uint32_t n = window_size; n > 1; n >>= 1) ++window_bits;

    std::vector<uint8_t> output(output_len);

    mspack_system sys{};
    sys.open = mem_open;
    sys.close = mem_close;
    sys.read = mem_read;
    sys.write = mem_write;
    sys.seek = mem_seek;
    sys.tell = mem_tell;
    sys.message = mem_message;
    sys.alloc = mem_alloc;
    sys.free = mem_free;
    sys.copy = mem_copy;

    MemoryFile in{};
    in.buffer = &input;
    in.offset = 0;
    MemoryFile out{};
    out.buffer = &output;
    out.offset = 0;

    auto* lzxd = lzxd_init(&sys,
                           reinterpret_cast<mspack_file*>(&in),
                           reinterpret_cast<mspack_file*>(&out),
                           static_cast<int>(window_bits),
                           0,
                           0x8000,
                           static_cast<off_t>(output_len),
                           0);
    if (!lzxd) throw std::runtime_error("lzxd_init failed");

    if (!reference.empty()) {
      const size_t padding = window_size - reference.size();
      std::memset(&lzxd->window[0], 0, padding);
      std::memcpy(&lzxd->window[padding], reference.data(), reference.size());
      lzxd->ref_data_size = window_size;
    }

    const int result = lzxd_decompress(lzxd, static_cast<off_t>(output_len));
    lzxd_free(lzxd);
    if (result != MSPACK_ERR_OK) {
      std::cerr << "lzxd_decompress failed: " << result << "\n";
      return 3;
    }

    WriteAll(argv[5], output);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 4;
  }
}
