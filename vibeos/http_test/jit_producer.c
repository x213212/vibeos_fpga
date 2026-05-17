int main() {
    volatile int *s = (volatile int *)shared_mem();
    s[0] = 0; /* full flag */
    s[1] = 0; /* value */
    s[2] = 0; /* produced count */
    s[3] = 0; /* done flag */

    print("producer start shared=%x size=%d\n", s, shared_size());

    for (int i = 1; i <= 100; i++) {
        while (s[0] != 0) yield();
        s[1] = i;
        s[0] = 1;
        s[2] = i;
        if ((i % 20) == 0) print("producer produced=%d\n", i);
        yield();
    }

    while (s[0] != 0) yield();
    s[3] = 1;
    print("producer done\n");
    return 0;
}
