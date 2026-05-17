int main() {
    volatile int *s = (volatile int *)shared_mem();
    int count = 0;
    int sum = 0;

    print("consumer start shared=%x size=%d\n", s, shared_size());

    while (!s[3] || s[0] != 0) {
        if (s[0] == 1) {
            int v = s[1];
            sum += v;
            count++;
            s[0] = 0;
            if ((count % 20) == 0) {
                print("consumer count=%d sum=%d last=%d\n", count, sum, v);
            }
        }
        yield();
    }

    print("consumer done count=%d sum=%d\n", count, sum);
    return 0;
}
