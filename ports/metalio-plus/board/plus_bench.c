// Shared speed probe: this exact file is compiled into both the stage-1 smoke
// app (components/plus_board) and CrossNext (board/), so the numbers compare
// like for like. Loops: ALU-only and 16 KB internal-RAM reads, each placed once
// in IRAM and once in flash.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <esp_attr.h>
#include <esp_cpu.h>
#include <esp_heap_caps.h>

#include "plus_bench.h"

static volatile uint32_t s_sink;

#define ALU_BODY                                       \
  uint32_t a = seed, b = 0x9E3779B9u;                  \
  for (uint32_t i = 0; i < iters; ++i) {               \
    a = (a ^ (a << 5)) + b;                            \
    b = (b >> 3) ^ a;                                  \
  }                                                    \
  return a ^ b;

#define READ_BODY                                      \
  uint32_t acc = 0;                                    \
  const size_t n = bytes / 4;                          \
  for (int k = 0; k < passes; ++k)                     \
    for (size_t i = 0; i < n; i += 4) acc += w[i] ^ w[i + 1] ^ w[i + 2] ^ w[i + 3]; \
  return acc;

static IRAM_ATTR __attribute__((noinline)) uint32_t alu_iram(uint32_t seed, uint32_t iters) { ALU_BODY }
static __attribute__((noinline)) uint32_t alu_flash(uint32_t seed, uint32_t iters) { ALU_BODY }
static IRAM_ATTR __attribute__((noinline)) uint32_t read_iram(const uint32_t* w, size_t bytes, int passes) { READ_BODY }
static __attribute__((noinline)) uint32_t read_flash(const uint32_t* w, size_t bytes, int passes) { READ_BODY }

// cycles per inner iteration x100
static unsigned cpi100(uint32_t cycles, uint32_t iters) { return (unsigned)((uint64_t)cycles * 100 / iters); }

void plus_bench_run(char* out, size_t out_len) {
  const uint32_t iters = 200000;
  uint32_t c0 = esp_cpu_get_cycle_count();
  s_sink = alu_iram(1, iters);
  const uint32_t alu_i = esp_cpu_get_cycle_count() - c0;
  c0 = esp_cpu_get_cycle_count();
  s_sink = alu_flash(2, iters);
  const uint32_t alu_f = esp_cpu_get_cycle_count() - c0;

  unsigned rd_i = 0, rd_f = 0;
  uint32_t* buf = heap_caps_malloc(16384, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (buf) {
    memset(buf, 0x5A, 16384);
    s_sink = read_iram(buf, 16384, 1);  // warm
    c0 = esp_cpu_get_cycle_count();
    s_sink = read_iram(buf, 16384, 32);
    rd_i = cpi100(esp_cpu_get_cycle_count() - c0, 32 * 16384 / 16);
    c0 = esp_cpu_get_cycle_count();
    s_sink = read_flash(buf, 16384, 32);
    rd_f = cpi100(esp_cpu_get_cycle_count() - c0, 32 * 16384 / 16);
    heap_caps_free(buf);
  }
  snprintf(out, out_len,
           "shared bench (cycles/iter x100): alu iram %u flash %u | read16K iram %u flash %u | buf %p",
           cpi100(alu_i, iters), cpi100(alu_f, iters), rd_i, rd_f, (void*)buf);
}
