#include "app.h"

int main() {
    FILE *fp = fopen("./remove_me.txt", "w");
    const char *msg = "delete me\n";

    if (!fp) {
        print("file_remove: create failed\n");
        return 1;
    }
    fwrite(msg, 1, strlen(msg), fp);
    fclose(fp);
    print("file_remove: created\n");

    if (appfs_unlink("./remove_me.txt") != 0) {
        print("file_remove: remove failed\n");
        return 2;
    }

    print("file_remove: removed\n");
    return 0;
}
