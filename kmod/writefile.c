#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

static void bad_usage() {
    fprintf(stderr, "bad usage\n");
    exit(2);
}

int main(int argc, const char** argv) {
    ssize_t file_size = -1;
    ssize_t buf_size = -1; // if -1, all in one go
    const char* filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-buf-size") == 0) {
            if (i+1 >= argc) { bad_usage(); } i++;
            buf_size = strtoull(argv[i], NULL, 0);
            if (buf_size == ULLONG_MAX) {
                perror("bad bad_size");
                exit(1);
            }
        } else if (strcmp(argv[i], "-size") == 0) {
            if (i+1 >= argc) { bad_usage(); } i++;
            file_size = strtoull(argv[i], NULL, 0);
            if (file_size == ULLONG_MAX) {
                perror("bad bad_size");
                exit(1);
            }
        } else {
            if (filename != NULL) { bad_usage(); }
            filename = argv[i];
        }
    }
    if (buf_size < 0) { buf_size = file_size; }
    if (file_size < 0 || filename == NULL) { bad_usage(); }

    printf("writing %ld bytes with bufsize %ld to %s\n", file_size, buf_size, filename);

    int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0) {
        perror("open");
        exit(1);
    }

    unsigned char* buffer = malloc(buf_size);
    if (buffer == NULL) {
        perror("malloc");
        exit(1);
    }

    while (file_size > 0) {
        ssize_t res = write(fd, buffer, file_size > buf_size ? buf_size : file_size);
        if (res < 0) {
            perror("write");
            exit(1);
        }
        file_size -= res;
    }

    printf("finished writing, will now close\n");

    if (close(fd) < 0) {
        perror("close");
        exit(1);
    }

    printf("done.\n");

    return 0;
}