/*
 * kernel.h: the small runtime crosshair brings along, for x86-64 Linux.
 *
 * The program is built without a C library,
 * so this file provides what one would:
 * the entry point that calls main(), the system calls,
 * and the two memory functions the compiler may call by itself.
 * The binary is fully static with nothing to relocate:
 * the kernel maps it and jumps to _start,
 * and main() runs a few instructions later.
 * No loader, allocator, locale or stdio runs before main() or after it.
 * The program keeps no global variables either, the environment included,
 * so the binary has no writable segment: the kernel maps one segment, code and constants.
 *
 * The instrumented build for profile-guided optimisation defines
 * CROSSHAIR_HOSTED: there the C library supplies the entry point
 * and the memory functions, because the profiling runtime needs it
 * to write the profile when main() returns.
 */

#pragma once

#if !defined(__x86_64__) || !defined(__linux__)
#error "crosshair calls the Linux kernel directly and supports x86-64 only"
#endif

#include <asm/signal.h>
#include <asm/unistd.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/prctl.h>
#include <linux/signalfd.h>
#include <linux/un.h>
#include <linux/utsname.h>
#include <stddef.h>
#include <stdint.h>

enum { STDIN_FD = 0, STDOUT_FD = 1, STDERR_FD = 2 };
enum { EXIT_SUCCESS = 0, EXIT_FAILURE = 1 };

/* A Unix stream socket: the kernel's UAPI headers leave these to the C library. */
enum { AF_UNIX = 1, SOCK_STREAM = 1, SOCK_CLOEXEC = O_CLOEXEC };

/* ---- System calls ---- */

/*
 * The number goes in rax and the arguments in rdi, rsi, rdx and r10.
 * The result comes back in rax, where -4095 to -1 mean an error (-errno).
 * The syscall instruction itself overwrites rcx and r11.
 * One function per argument count, so no register is loaded in vain.
 */

static inline long system_call0(long number)
{
    long result;

    __asm__ volatile("syscall" : "=a"(result) : "a"(number) : "rcx", "r11", "memory");

    return result;
}

static inline long system_call1(long number, long first)
{
    long result;

    __asm__ volatile("syscall" : "=a"(result) : "a"(number), "D"(first) : "rcx", "r11", "memory");

    return result;
}

static inline long system_call2(long number, long first, long second)
{
    long result;

    __asm__ volatile("syscall"
                     : "=a"(result)
                     : "a"(number), "D"(first), "S"(second)
                     : "rcx", "r11", "memory");

    return result;
}

static inline long system_call3(long number, long first, long second, long third)
{
    long result;

    __asm__ volatile("syscall"
                     : "=a"(result)
                     : "a"(number), "D"(first), "S"(second), "d"(third)
                     : "rcx", "r11", "memory");

    return result;
}

static inline long system_call4(long number, long first, long second, long third, long fourth)
{
    register long fourth_register __asm__("r10") = fourth;
    long result;

    __asm__ volatile("syscall"
                     : "=a"(result)
                     : "a"(number), "D"(first), "S"(second), "d"(third), "r"(fourth_register)
                     : "rcx", "r11", "memory");

    return result;
}

/* The calls crosshair makes, with the kernel's own parameters. */

static inline long sys_read(int fd, void *buffer, size_t length)
{
    return system_call3(__NR_read, fd, (long)buffer, (long)length);
}

static inline long sys_write(int fd, const void *buffer, size_t length)
{
    return system_call3(__NR_write, fd, (long)buffer, (long)length);
}

static inline long sys_open(const char *path, int flags, int mode)
{
    return system_call3(__NR_open, (long)path, flags, mode);
}

static inline long sys_close(int fd)
{
    return system_call1(__NR_close, fd);
}

static inline long sys_unlink(const char *path)
{
    return system_call1(__NR_unlink, (long)path);
}

static inline long sys_poll(struct pollfd *watched, unsigned count, int timeout_ms)
{
    return system_call3(__NR_poll, (long)watched, count, timeout_ms);
}

static inline long sys_socket(int domain, int type, int protocol)
{
    return system_call3(__NR_socket, domain, type, protocol);
}

static inline long sys_connect(int fd, const struct sockaddr_un *address, unsigned length)
{
    return system_call3(__NR_connect, fd, (long)address, length);
}

static inline long sys_fork(void)
{
    return system_call0(__NR_fork);
}

static inline long sys_setsid(void)
{
    return system_call0(__NR_setsid);
}

static inline long sys_dup2(int fd, int new_fd)
{
    return system_call2(__NR_dup2, fd, new_fd);
}

static inline long sys_kill(int pid, int signal)
{
    return system_call2(__NR_kill, pid, signal);
}

static inline long sys_getpid(void)
{
    return system_call0(__NR_getpid);
}

static inline long sys_getppid(void)
{
    return system_call0(__NR_getppid);
}

static inline long sys_getuid(void)
{
    return system_call0(__NR_getuid);
}

static inline long sys_uname(struct new_utsname *names)
{
    return system_call1(__NR_uname, (long)names);
}

static inline long sys_prctl(int option, unsigned long argument)
{
    return system_call2(__NR_prctl, option, (long)argument);
}

/* The kernel's signal set is one 64-bit word, bit N - 1 for signal N. */
static inline uint64_t signal_bit(int signal)
{
    return 1ULL << (signal - 1);
}

static inline long sys_block_signals(const uint64_t *signals)
{
    return system_call4(__NR_rt_sigprocmask, SIG_BLOCK, (long)signals, 0, sizeof *signals);
}

static inline long sys_signalfd(const uint64_t *signals, int flags)
{
    return system_call4(__NR_signalfd4, -1, (long)signals, sizeof *signals, flags);
}

[[noreturn]] static inline void sys_exit(int status)
{
    __asm__ volatile("syscall" : : "a"(__NR_exit_group), "D"(status) : "rcx", "r11", "memory");
    __builtin_unreachable();
}

/* ---- The entry point ---- */

/* main() takes the environment as its third argument, as the C library also passes it. */
int main(int argc, char **argv, char **environment);

#ifndef CROSSHAIR_HOSTED

/*
 * Called by _start with the initial stack:
 * argc, then argv and its null, then the environment and its null.
 * It has external linkage so that _start can call it by name.
 */
[[noreturn]] void start_program(long *stack)
{
    int argc = (int)stack[0];
    char **argv = (char **)&stack[1];

    sys_exit(main(argc, argv, &argv[argc + 1]));
}

/*
 * The kernel starts the process here, with no return address on the stack.
 * A zero frame pointer ends stack traces,
 * and the call needs the stack aligned to 16 bytes.
 */
__asm__(".text\n"
        ".global _start\n"
        ".type _start, @function\n"
        "_start:\n"
        "    xor %ebp, %ebp\n"
        "    mov %rsp, %rdi\n"
        "    and $-16, %rsp\n"
        "    call start_program\n"
        "    hlt\n");

#endif

/* ---- Memory ---- */

/* Copy LENGTH bytes upwards, so the destination may overlap the source from below. */
static inline void copy_bytes(void *destination, const void *source, size_t length)
{
    __asm__ volatile("rep movsb" : "+D"(destination), "+S"(source), "+c"(length) : : "memory");
}

#ifndef CROSSHAIR_HOSTED

/*
 * The compiler calls these two for copies and fills it does not expand inline,
 * even in a freestanding program.
 * The string instructions are the shortest code for both,
 * and fast for every size on CPUs with FSRM, Alder Lake among them.
 */

void *memcpy(void *restrict destination, const void *restrict source, size_t length)
{
    copy_bytes(destination, source, length);

    return destination;
}

void *memset(void *destination, int value, size_t length)
{
    void *cursor = destination;

    __asm__ volatile("rep stosb" : "+D"(cursor), "+c"(length) : "a"(value) : "memory");

    return destination;
}

#endif
