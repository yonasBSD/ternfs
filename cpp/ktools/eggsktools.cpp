#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define die(fmt, ...) do { fprintf(stderr, fmt "\n" __VA_OPT__(,) __VA_ARGS__); exit(1); } while(false)

const char* exe = NULL;

#define badUsage(...) do { \
        fprintf(stderr, "Bad usage, expecting %s writefile|readfile <command arguments>\n", exe); \
        __VA_OPT__(fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n");) \
        exit(2); \
    } while(0) \

static uint64_t nanosNow() {
    struct timespec tp;
    if (clock_gettime(CLOCK_REALTIME, &tp) < 0) {
        die("could not get time: %d (%s)", errno, strerror(errno));
    }
    return tp.tv_sec*1000000000ull + tp.tv_nsec;
}

// Just a super dumb file test, to have a controlled environment
// where every syscall is accounted for.
static void writeFile(int argc, const char** argv) {
    ssize_t fileSize = -1;
    ssize_t bufSize = -1; // if -1, all in one go
    const char* filename = NULL;

    for (int i = 0; i < argc; i++) {
        if (std::string(argv[i]) == "-buf-size") {
            if (i+1 >= argc) { badUsage("No argument after -buf-size"); } i++;
            bufSize = strtoull(argv[i], NULL, 0);
            if (bufSize == ULLONG_MAX) {
                badUsage("Bad -buf-size: %d (%s)", errno, strerror(errno));
            }
        } else if (std::string(argv[i]) == "-size") {
            if (i+1 >= argc) { badUsage("No argument after -size"); } i++;
            fileSize = strtoull(argv[i], NULL, 0);
            if (fileSize == ULLONG_MAX) {
                badUsage("Bad -size: %d (%s)", errno, strerror(errno));
            }
        } else {
            if (filename != NULL) { badUsage("Filename already specified: %s", filename); }
            filename = argv[i];
        }
    }

    if (bufSize < 0) { bufSize = fileSize; }
    if (fileSize < 0 || filename == NULL) { badUsage("No -size specified"); }

    printf("writing %ld bytes with bufsize %ld to %s\n", fileSize, bufSize, filename);

    int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0) {
        die("could not open file %s: %d (%s)", filename, errno, strerror(errno));
    }

    uint8_t* buffer = (uint8_t*)malloc(bufSize);
    if (buffer == NULL) {
        die("could not allocate: %d (%s)", errno, strerror(errno));
    }

    uint64_t start = nanosNow();

    ssize_t toWrite = fileSize;
    while (toWrite > 0) {
        ssize_t res = write(fd, buffer, toWrite > bufSize ? bufSize : toWrite);
        if (res < 0) {
            die("couldn't write %s: %d (%s)", filename, errno, strerror(errno));
        }
        toWrite -= res;
    }

    printf("finished writing, will now close\n");

    if (close(fd) < 0) {
        die("couldn't close %s: %d (%s)", filename, errno, strerror(errno));
    }

    uint64_t elapsed = nanosNow() - start;
    printf("done (%fGB/s).\n", (double)fileSize/(double)elapsed);
}

// Same as writeFile, but for reading.
static void readFile(int argc, const char** argv) {
    ssize_t bufSize = -1; // if -1, all in one go
    const char* filename = NULL;

    for (int i = 0; i < argc; i++) {
        if (std::string(argv[i]) == "-buf-size") {
            if (i+1 >= argc) { badUsage("No argument after -buf-size"); } i++;
            bufSize = strtoull(argv[i], NULL, 0);
            if (bufSize == ULLONG_MAX) {
                badUsage("Bad -buf-size: %d (%s)", errno, strerror(errno));
            }
        } else {
            if (filename != NULL) { badUsage("Filename already specified: %s", filename); }
            filename = argv[i];
        }
    }

    size_t fileSize;
    {
        struct stat st;
        if(stat(filename, &st) != 0) {
            die("couldn't stat %s: %d (%s)", filename, errno, strerror(errno));
        }
        fileSize = st.st_size;
    }

    if (bufSize < 0) { bufSize = fileSize; }

    printf("reading %ld bytes with bufsize %ld to %s\n", fileSize, bufSize, filename);

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        die("could not open file %s: %d (%s)", filename, errno, strerror(errno));
    }

    uint8_t* buffer = (uint8_t*)malloc(bufSize);
    if (buffer == NULL) {
        die("could not allocate: %d (%s)", errno, strerror(errno));
    }

    uint64_t start = nanosNow();

    size_t readSize = 0;
    for (;;) {
        ssize_t ret = read(fd, buffer, bufSize);
        if (ret < 0) {
            die("could not read file %s: %d (%s)", filename, errno, strerror(errno));
        }
        if (ret == 0) { break; }
        readSize += ret;
    }

    if (readSize != fileSize) {
        die("expected to read %lu (file size), but read %lu instead", fileSize, readSize);
    }

    printf("finished reading, will now close\n");

    if (close(fd) < 0) {
        die("couldn't close %s: %d (%s)", filename, errno, strerror(errno));
    }

    uint64_t elapsed = nanosNow() - start;

    printf("done (%fGB/s).\n", (double)fileSize/(double)elapsed);
}

// Same as writeFile, but for reading.
static void readLink(int argc, const char** argv) {
    const char* filename = NULL;

    for (int i = 0; i < argc; i++) {
        if (filename != NULL) { badUsage("Filename already specified: %s", filename); }
        filename = argv[i];
    }

    char buf[1024];
    ssize_t ret = readlink(filename, buf, sizeof(buf));
    if (ret < 0) {
        die("could not readlink %s: %d (%s)", filename, errno, strerror(errno));
    }
    
    printf("link: \"");
    for (int i = 0; i < ret; i++) {
        if (isprint(buf[i])) {
            printf("%c", buf[i]);
        } else {
            printf("\\%02x", buf[i]);
        }
    }
    printf("\"\n");
}

int main(int argc, const char** argv) {
    exe = argv[0];

    if (argc < 2) { badUsage("No command"); }

    std::string cmd(argv[1]);

    if (cmd == "writefile") {
        writeFile(argc - 2, argv + 2);
    } else if (cmd == "readfile") {
        readFile(argc - 2, argv + 2);
    } else if (cmd == "readlink") {
        readLink(argc - 2, argv + 2);
    } else {
        badUsage("Bad command %s", cmd.c_str());
    }

    return 0;
}