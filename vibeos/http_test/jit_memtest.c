int main() {
    int sizes[4];
    sizes[0] = 1024 * 1024;
    sizes[1] = 4 * 1024 * 1024;
    sizes[2] = 8 * 1024 * 1024;
    sizes[3] = 12 * 1024 * 1024;

    print("JIT memtest start\n");

    for (int s = 0; s < 4; s++) {
        int size = sizes[s];
        unsigned char *p = malloc(size);
        print("malloc size=%d ptr=%x\n", size, p);
        if (!p) {
            print("malloc failed at size=%d\n", size);
            continue;
        }

        for (int i = 0; i < size; i++) {
            p[i] = (unsigned char)((i * 131 + s) & 255);
        }

        int bad = -1;
        for (int i = 0; i < size; i++) {
            unsigned char want = (unsigned char)((i * 131 + s) & 255);
            if (p[i] != want) {
                bad = i;
                print("verify bad size=%d at=%d got=%d want=%d\n",
                      size, i, p[i], want);
                break;
            }
        }

        if (bad < 0) {
            print("verify ok size=%d first=%d last=%d\n",
                  size, p[0], p[size - 1]);
        }

        free(p);
        print("free ok size=%d\n", size);
    }

    int csize = 2 * 1024 * 1024;
    unsigned char *z = calloc(csize, 1);
    print("calloc size=%d ptr=%x\n", csize, z);
    if (z) {
        int bad_zero = -1;
        for (int i = 0; i < csize; i++) {
            if (z[i] != 0) {
                bad_zero = i;
                print("calloc bad at=%d got=%d\n", i, z[i]);
                break;
            }
        }
        if (bad_zero < 0) print("calloc zero ok\n");
        free(z);
    }

    for (int round = 0; round < 5; round++) {
        int size = 4 * 1024 * 1024;
        unsigned char *p = malloc(size);
        print("reuse round=%d ptr=%x\n", round, p);
        if (!p) {
            print("reuse malloc failed\n");
            return 1;
        }
        p[0] = (unsigned char)(round + 1);
        p[size - 1] = (unsigned char)(round + 101);
        print("reuse edge first=%d last=%d\n", p[0], p[size - 1]);
        free(p);
    }

    print("JIT memtest done\n");
    return 0;
}
