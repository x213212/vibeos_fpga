#include "app.h"

int main() {
    FILE *fp;
    char buf[64];
    int n;
    const char *msg = "myapp file io ok\n";

    print("file_io: write start\n");
    fp = fopen("./myapp_note.txt", "w");
    if (!fp) {
        print("file_io: fopen write failed\n");
        return 1;
    }
    fwrite(msg, 1, strlen(msg), fp);
    fclose(fp);

    print("file_io: read start\n");
    fp = fopen("./myapp_note.txt", "r");
    if (!fp) {
        print("file_io: fopen read failed\n");
        return 2;
    }
    n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
    if (n < 0) n = 0;
    buf[n] = '\0';
    fclose(fp);

    print("file_io: content=\n");
    print(buf);
    return 0;
}
