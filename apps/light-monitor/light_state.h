/*
 * 业务规则：根据原始光线值，判断当前是否“偏暗”。
 * 与 I²C、寄存器读写无关，输入数字就能测试，因此放在独立头文件里。
 *
 * 为什么需要回差（hysteresis）？
 * 如果只用一个阈值 10，读数在 9、10、9、10 波动时，状态会反复切换。
 * 这里设两个边界：低于 10 才进入偏暗，达到 15 才恢复正常。
 * 10～14 这个区间保持上一次状态，避免频繁变化。
 *
 * 参数：
 *   raw           本次原始值；不直接等于 lux。
 *   threshold     进入偏暗的阈值，使用严格小于。
 *   hysteresis    恢复阈值额外增加的数值。
 *   was_dark      上次是否偏暗。
 *   first_sample  是否是第一个样本，首样本不依赖历史状态。
 * 调用者须保证 threshold+hysteresis 不超过 65535。
 * 返回 true=偏暗，false=正常。
 */

/* 头文件保护：同一编译单元重复包含此文件时，避免重复定义。 */
#ifndef LIGHT_STATE_H
#define LIGHT_STATE_H

#include <stdbool.h>
#include <stdint.h>

/* static：函数只在包含它的编译单元里可见。
 * inline：允许编译器内联；它是优化提示，并不保证一定没有函数调用。 */
static inline bool light_is_dark(uint16_t raw, unsigned threshold,
                                 unsigned hysteresis, bool was_dark,
                                 bool first_sample)
{
    /* 首次采样或原来正常：raw < threshold 才进入偏暗。
     * 例如 9 -> true，10 -> false。 */
    if (first_sample || !was_dark)
        return raw < threshold;
    /* 原来已经偏暗：只要低于恢复阈值，就继续保持偏暗。
     * 例如阈值10、回差5：14仍偏暗，15恢复正常。
     * 这里没有修改外部变量，main() 用返回值更新自己的 dark 变量。 */
    return raw < threshold + hysteresis;
}

/* 三档状态：enum 给整数起名字，让代码比 0、1、2 更容易读懂。 */
enum light_state { LIGHT_DARK, LIGHT_NORMAL, LIGHT_BRIGHT };

/* 默认阈值为 10、100，回差为 5 时：
 * 第一次采样：<10 为 dark，10～99 为 normal，>=100 为 bright。
 * 已经 dark：达到 15 才退出 dark。
 * 已经 bright：低于 95 才退出 bright。
 * 读数大幅变化时允许直接从 dark 跳到 bright，反过来也一样。
 * 调用者保证 dark_threshold+hysteresis < bright_threshold-hysteresis。
 */
static inline enum light_state light_classify(uint16_t raw, unsigned dark_threshold,
                                               unsigned bright_threshold, unsigned hysteresis,
                                               enum light_state previous, bool first_sample)
{
    if (!first_sample) {
        if (previous == LIGHT_DARK && raw < dark_threshold + hysteresis)
            return LIGHT_DARK;
        if (previous == LIGHT_BRIGHT && raw >= bright_threshold - hysteresis)
            return LIGHT_BRIGHT;
    }
    if (raw < dark_threshold)
        return LIGHT_DARK;
    if (raw >= bright_threshold)
        return LIGHT_BRIGHT;
    return LIGHT_NORMAL;
}

/* 把程序内部的状态转换为终端/JSON 输出的文字。 */
static inline const char *light_state_name(enum light_state state)
{
    switch (state) {
    case LIGHT_DARK:
        return "dark";
    case LIGHT_BRIGHT:
        return "bright";
    default:
        return "normal";
    }
}

#endif
