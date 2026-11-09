#include <assert.h>
#include <string.h>
#include "../apps/light-monitor/light_state.h"

int main(void)
{
    assert(light_is_dark(99, 100, 20, false, true));
    assert(!light_is_dark(100, 100, 20, false, true));
    assert(light_is_dark(110, 100, 20, true, false));
    assert(!light_is_dark(120, 100, 20, true, false));
    assert(!light_is_dark(110, 100, 20, false, false));
    assert(light_is_dark(99, 100, 20, false, false));
    assert(!light_is_dark(0, 0, 0, true, false));
    assert(light_is_dark(65534, 65535, 0, false, true));
    assert(!light_is_dark(65535, 65535, 0, true, false));
    /* 首次采样不应依赖历史状态，检查两个边界。 */
    assert(light_classify(9, 10, 100, 5, LIGHT_BRIGHT, true) == LIGHT_DARK);
    assert(light_classify(10, 10, 100, 5, LIGHT_DARK, true) == LIGHT_NORMAL);
    assert(light_classify(99, 10, 100, 5, LIGHT_BRIGHT, true) == LIGHT_NORMAL);
    assert(light_classify(100, 10, 100, 5, LIGHT_NORMAL, true) == LIGHT_BRIGHT);
    /* 回差：暗状态到 15 恢复，亮状态低于 95 恢复。 */
    assert(light_classify(14, 10, 100, 5, LIGHT_DARK, false) == LIGHT_DARK);
    assert(light_classify(15, 10, 100, 5, LIGHT_DARK, false) == LIGHT_NORMAL);
    assert(light_classify(95, 10, 100, 5, LIGHT_BRIGHT, false) == LIGHT_BRIGHT);
    assert(light_classify(94, 10, 100, 5, LIGHT_BRIGHT, false) == LIGHT_NORMAL);
    assert(light_classify(99, 10, 100, 5, LIGHT_NORMAL, false) == LIGHT_NORMAL);
    assert(light_classify(10, 10, 100, 5, LIGHT_NORMAL, false) == LIGHT_NORMAL);
    /* 大幅变化可以直接跨档；回差为零时也能在边界正确切换。 */
    assert(light_classify(65535, 10, 100, 5, LIGHT_DARK, false) == LIGHT_BRIGHT);
    assert(light_classify(0, 10, 100, 5, LIGHT_BRIGHT, false) == LIGHT_DARK);
    assert(light_classify(10, 10, 100, 0, LIGHT_DARK, false) == LIGHT_NORMAL);
    assert(light_classify(99, 10, 100, 0, LIGHT_BRIGHT, false) == LIGHT_NORMAL);
    assert(strcmp(light_state_name(LIGHT_DARK), "dark") == 0);
    assert(strcmp(light_state_name(LIGHT_NORMAL), "normal") == 0);
    assert(strcmp(light_state_name(LIGHT_BRIGHT), "bright") == 0);
    return 0;
}
