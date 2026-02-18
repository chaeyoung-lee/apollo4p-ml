/**
 * Minimal newlib syscall stubs for bare-metal ARM.
 * Satisfies the linker; stdio (e.g. printf) may use am_util_stdio or UART elsewhere.
 */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    (void)st;
    return -1;
}

int _getpid(void)
{
    return 1;
}

int _isatty(int fd)
{
    (void)fd;
    return 0;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    return -1;
}

__attribute__((used)) void _exit(int status)
{
    (void)status;
    while (1)
        ;
}

off_t _lseek(int fd, off_t offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    return (off_t)-1;
}

int _read(int fd, void *buf, size_t count)
{
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

int _write(int fd, const void *buf, size_t count)
{
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}
