#ifndef SYS_STAT_H
#define SYS_STAT_H

typedef unsigned int mode_t;
typedef long off_t;

struct stat {
    mode_t st_mode;
    off_t st_size;
    off_t st_atime;
    off_t st_mtime;
    off_t st_ctime;
};

#endif
