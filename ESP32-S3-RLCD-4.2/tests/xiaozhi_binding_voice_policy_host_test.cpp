// 验证小智绑定码重复播报和数字筛选规则。
#include "xiaozhi_binding_voice.h"

#include <cassert>

int main()
{
    assert(!xiaozhi_binding_voice::should_announce(nullptr, ""));
    assert(!xiaozhi_binding_voice::should_announce("", ""));
    assert(!xiaozhi_binding_voice::should_announce("123456", "123456"));
    assert(xiaozhi_binding_voice::should_announce("123456", ""));
    assert(xiaozhi_binding_voice::should_announce("123456", nullptr));
    assert(xiaozhi_binding_voice::should_announce("654321", "123456"));

    for (int index = 0; index < 10; ++index) {
        assert(xiaozhi_binding_voice::digit_index(static_cast<char>('0' + index)) == index);
    }
    assert(xiaozhi_binding_voice::digit_index('/') == -1);
    assert(xiaozhi_binding_voice::digit_index(':') == -1);
    assert(xiaozhi_binding_voice::digit_index('A') == -1);
    return 0;
}
