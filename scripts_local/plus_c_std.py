"""C23 compatibility for the ESP-IDF 6 / GCC 15 toolchain (Metalio Plus builds).

GCC 15 defaults C to C23, where bool/true/false are keywords. ricmoo/QRCode's
qrcode.h typedefs its own bool for C translation units, which no longer
compiles. Narrow that typedef to pre-C23 C in the libdep copy (idempotent).
"""
Import("env")  # noqa: F821
import os

OLD = "#ifndef __cplusplus\ntypedef unsigned char bool;"
NEW = ("#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)\n"
       "typedef unsigned char bool;")


def _patch_qrcode():
    libdeps = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")  # noqa: F821
    if not os.path.isdir(libdeps):
        return
    for env_dir in os.listdir(libdeps):
        header = os.path.join(libdeps, env_dir, "QRCode", "src", "qrcode.h")
        if not os.path.isfile(header):
            continue
        with open(header, encoding="utf-8") as f:
            text = f.read()
        if OLD in text:
            with open(header, "w", encoding="utf-8") as f:
                f.write(text.replace(OLD, NEW, 1))
            print(f"plus_c_std: patched {header} for C23")


_patch_qrcode()
