// 声明 QWeather 响应缓冲、JSON 根对象和业务成功字段读取接口。
#pragma once

#include "cJSON.h"
#include "network_json_root.h"
#include "scoped_heap_buffer.h"

#include <stddef.h>

const char *qweather_stage_text(const char *stage);

class QweatherResponseBuffer {
public:
    QweatherResponseBuffer(const char *stage, size_t buffer_size);
    ~QweatherResponseBuffer();

    QweatherResponseBuffer(const QweatherResponseBuffer &) = delete;
    QweatherResponseBuffer &operator=(const QweatherResponseBuffer &) = delete;

    char *get() const
    {
        return data_.get();
    }

    size_t size() const
    {
        return size_;
    }

    explicit operator bool() const
    {
        return static_cast<bool>(data_);
    }

private:
    ScopedHeapBuffer<char> data_;
    size_t size_ = 0;
};

class QweatherJsonRoot {
public:
    explicit QweatherJsonRoot(char *response);
    ~QweatherJsonRoot();

    QweatherJsonRoot(const QweatherJsonRoot &) = delete;
    QweatherJsonRoot &operator=(const QweatherJsonRoot &) = delete;

    const cJSON *get() const
    {
        return root_.get();
    }

    explicit operator bool() const
    {
        return static_cast<bool>(root_);
    }

private:
    NetworkJsonRoot root_;
};

const char *qweather_json_string_value(const cJSON *item);
bool qweather_code_ok(const cJSON *code);
const char *qweather_code_text(const cJSON *code);
const cJSON *qweather_success_item(const cJSON *root, const char *field, const cJSON **code_out);
const cJSON *qweather_success_object(const cJSON *root, const char *field, const cJSON **code_out);
const cJSON *qweather_success_array(const cJSON *root, const char *field, const cJSON **code_out);
