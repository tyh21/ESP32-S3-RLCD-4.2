#pragma once

#include <stddef.h>
#include <stdint.h>

class Stream {
public:
    virtual ~Stream() = default;
    virtual size_t readBytes(uint8_t *buffer, size_t length) = 0;
    virtual size_t write(const uint8_t *buffer, size_t length) = 0;
    virtual size_t write(uint8_t value)
    {
        return write(&value, 1);
    }
    virtual void print(const char *) {}
    virtual void print(char value)
    {
        (void)write((uint8_t)value);
    }
    virtual void println() {}
};
