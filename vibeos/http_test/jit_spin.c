int main() {
    print("spin start\n");
    volatile unsigned int x = 0;
    while (1) {
        x++;
    }
}
