// 提供负责 malloc/calloc/兼容 heap_caps 分配与 free 的不可复制字节缓冲区所有权。
#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum class HeapBufferInit {
    kUninitialized,
    kZeroed,
    kCString,
};

template <typename Byte>
class ScopedHeapBuffer {
public:
    explicit ScopedHeapBuffer(size_t size,
                              HeapBufferInit init = HeapBufferInit::kUninitialized)
        : data_(allocate(size, init)),
          size_(size)
    {
        static_assert(sizeof(Byte) == 1, "ScopedHeapBuffer only owns byte-sized elements");
        if (data_ && size_ > 0 && init == HeapBufferInit::kCString) {
            data_[0] = Byte{};
        }
    }

    // 接管由 malloc/calloc/heap_caps_* 分配且可由 free() 释放的既有缓冲区。
    ScopedHeapBuffer(Byte *data, size_t size)
        : data_(data),
          size_(size)
    {
        static_assert(sizeof(Byte) == 1, "ScopedHeapBuffer only owns byte-sized elements");
    }

    ~ScopedHeapBuffer()
    {
        reset();
    }

    ScopedHeapBuffer(const ScopedHeapBuffer &) = delete;
    ScopedHeapBuffer &operator=(const ScopedHeapBuffer &) = delete;

    Byte *data() const
    {
        return data_;
    }

    Byte *get() const
    {
        return data_;
    }

    size_t size() const
    {
        return size_;
    }

    Byte *release()
    {
        Byte *data = data_;
        data_ = nullptr;
        size_ = 0;
        return data;
    }

    void reset()
    {
        free(data_);
        data_ = nullptr;
        size_ = 0;
    }

    void clear() const
    {
        if (data_) {
            memset(data_, 0, size_);
        }
    }

    explicit operator bool() const
    {
        return data_ != nullptr;
    }

private:
    static Byte *allocate(size_t size, HeapBufferInit init)
    {
        if (size == 0) {
            return nullptr;
        }
        void *memory = init == HeapBufferInit::kZeroed
                           ? calloc(size, sizeof(Byte))
                           : malloc(size * sizeof(Byte));
        return static_cast<Byte *>(memory);
    }

    Byte *data_ = nullptr;
    size_t size_ = 0;
};
