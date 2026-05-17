int main() {
    int sizes[4];
    sizes[0] = 4096;
    sizes[1] = 64 * 1024;
    sizes[2] = 512 * 1024;
    sizes[3] = 1024 * 1024;

    print("JIT kmemtest start\n");

    for (int s = 0; s < 4; s++) {
        int size = sizes[s];
        unsigned char *p = kmalloc(size);
        print("kmalloc size=%d ptr=%x\n", size, p);
        if (!p) {
            print("kmalloc failed at size=%d\n", size);
            continue;
        }

        for (int i = 0; i < size; i++) {
            p[i] = (unsigned char)((i * 73 + s + 11) & 255);
        }

        int bad = -1;
        for (int i = 0; i < size; i++) {
            unsigned char want = (unsigned char)((i * 73 + s + 11) & 255);
            if (p[i] != want) {
                bad = i;
                print("kverify bad size=%d at=%d got=%d want=%d\n",
                      size, i, p[i], want);
                break;
            }
        }

        if (bad < 0) {
            print("kverify ok size=%d first=%d last=%d\n",
                  size, p[0], p[size - 1]);
        }

        kfree(p);
        print("kfree ok size=%d\n", size);
    }

    for (int round = 0; round < 8; round++) {
        int size = 128 * 1024;
        unsigned char *p = kmalloc(size);
        print("kreuse round=%d ptr=%x\n", round, p);
        if (!p) {
            print("kreuse kmalloc failed\n");
            return 1;
        }
        p[0] = (unsigned char)(round + 7);
        p[size - 1] = (unsigned char)(round + 77);
        print("kreuse edge first=%d last=%d\n", p[0], p[size - 1]);
        kfree(p);
    }

    print("JIT kmemtest done\n");
    return 0;
}
