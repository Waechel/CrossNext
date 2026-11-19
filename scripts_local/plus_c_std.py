"""Compile C sources as gnu17 for the ESP-IDF 6 / GCC 15 toolchain (Metalio Plus).

GCC 15 defaults C to C23, where bool/true/false are keywords; third-party C
libraries that typedef their own bool (ricmoo/QRCode) no longer compile.
"""
Import("env")  # noqa: F821
env.Append(CFLAGS=["-std=gnu17"])  # noqa: F821
