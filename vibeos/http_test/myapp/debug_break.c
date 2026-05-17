int main() {
    int x = 42;
    int y = x + 7;

    print("debug_break: before\n");
    debug_line(6);
    debug_break();
    print("debug_break: after\n");
    printf("x=%d y=%d\n", x, y);
    return 0;
}
