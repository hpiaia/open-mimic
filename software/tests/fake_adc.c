// SPDX-License-Identifier: AGPL-3.0-only
// Test-only ADC open/ioctl adapter. Used only inside an isolated Linux container.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static int adc_fds[2] = {-1, -1};
static unsigned char* adc_mappings[2] = {NULL, NULL};
static uint32_t cursor = 32;

static void advance_frames(uint64_t nanoseconds)
{
    if (adc_mappings[0] == NULL || adc_mappings[1] == NULL)
    {
        return;
    }
    static uint64_t previous = 0;
    if (previous == 0)
    {
        previous = nanoseconds;
        return;
    }
    uint64_t frames = (nanoseconds - previous) * 11280 / 1000000000;
    if (frames == 0)
    {
        // There is no independent DMA producer in this deterministic fixture.
        // Ensure progress between observations even when an optimized snapshot
        // completes within a single synthetic frame period.
        frames = 1;
        previous = nanoseconds;
    }
    else
    {
        previous += frames * 1000000000 / 11280;
    }
    const char* directory = getenv("OPEN_MIMIC_TEST_ADC_DIRECTORY");
    char mode_path[4096];
    snprintf(mode_path, sizeof(mode_path), "%s/mode", directory);
    FILE* mode_file = fopen(mode_path, "r");
    const int mode = mode_file == NULL ? 0 : fgetc(mode_file);
    if (mode_file != NULL)
    {
        fclose(mode_file);
    }
    const uint64_t milliseconds = nanoseconds / 1000000;
    const uint16_t amplitude = mode == 'h' && milliseconds % 450 < 10 ? 700 : 0;
    // Update the file-backed rings on observation using a measured-size frame
    // cadence. This is an IPC/persistence fixture, not a real DMA timing test.
    for (uint64_t frame = 0; frame < frames; ++frame)
    {
        const uint32_t offset = cursor % 4096;
        memset(adc_mappings[0] + offset, 0, 32);
        memset(adc_mappings[1] + offset, 0, 32);
        adc_mappings[1][offset + 20] = (unsigned char)amplitude;
        adc_mappings[1][offset + 21] = (unsigned char)(amplitude >> 8);
        adc_mappings[1][offset + 22] = (unsigned char)(amplitude / 8);
        for (unsigned adc = 0; adc < 2; ++adc)
        {
            for (unsigned channel = 0; channel < 16; ++channel)
            {
                adc_mappings[adc][offset + channel * 2 + 1] |= (unsigned char)(channel << 4);
            }
        }
        cursor = offset + 32;
        memcpy(adc_mappings[0] + 4096, &cursor, 4);
        memcpy(adc_mappings[1] + 4096, &cursor, 4);
    }
}

int open(const char* path, int flags, ...)
{
    int (*real_open)(const char*, int, ...) = dlsym(RTLD_NEXT, "open");
    const char* directory = getenv("OPEN_MIMIC_TEST_ADC_DIRECTORY");
    if (directory != NULL
        && (strcmp(path, "/dev/ads7953.0") == 0 || strcmp(path, "/dev/ads7953.1") == 0))
    {
        const int index = path[strlen(path) - 1] - '0';
        char redirected[4096];
        if (snprintf(redirected, sizeof(redirected), "%s/adc%d", directory, index)
            >= (int)sizeof(redirected))
        {
            errno = ENAMETOOLONG;
            return -1;
        }
        adc_fds[index] = real_open(redirected, (flags & ~O_ACCMODE) | O_RDWR);
        if (adc_fds[index] >= 0)
        {
            void* mapped = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, adc_fds[index], 0);
            if (mapped != MAP_FAILED)
            {
                adc_mappings[index] = mapped;
                memcpy(adc_mappings[index] + 4096, &cursor, 4);
            }
        }
        return adc_fds[index];
    }
    if (flags & O_CREAT)
    {
        va_list arguments;
        va_start(arguments, flags);
        const int mode = va_arg(arguments, int);
        va_end(arguments);
        return real_open(path, flags, mode);
    }
    return real_open(path, flags);
}

int clock_gettime(clockid_t clock, struct timespec* value)
{
    const int result = (int)syscall(SYS_clock_gettime, clock, value);
    if (result == 0 && clock == CLOCK_MONOTONIC)
    {
        advance_frames((uint64_t)value->tv_sec * 1000000000 + (uint64_t)value->tv_nsec);
    }
    return result;
}

int ioctl(int fd, unsigned long request, ...)
{
    if (fd >= 0 && (fd == adc_fds[0] || fd == adc_fds[1]))
    {
        if (request == 0x4101)
        {
            return 8192;
        }
        if (request == 0x4102)
        {
            return 4096;
        }
        if (request == 0x4103)
        {
            return 32;
        }
    }
    errno = ENOTTY;
    return -1;
}
