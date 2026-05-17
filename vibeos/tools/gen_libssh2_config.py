#!/usr/bin/env python3
import sys
from pathlib import Path

CONFIG = """#ifndef LIBSSH2_CONFIG_H
#define LIBSSH2_CONFIG_H

#define HAVE_SNPRINTF 1
#define HAVE_VSNPRINTF 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_SYS_TIME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_SYS_UIO_H 1
#define HAVE_SELECT 1
#define HAVE_SYS_SELECT_H 1

#define LIBSSH2_MBEDTLS 1

/* Keep the surface area small for the bare-metal build. */
#define LIBSSH2_NO_DEPRECATED 1

#endif
"""


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: gen_libssh2_config.py <output.h>')
    dst = Path(sys.argv[1])
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(CONFIG, newline='\n')


if __name__ == '__main__':
    main()
