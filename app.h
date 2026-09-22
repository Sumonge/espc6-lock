/*
 * app.h —— 框架与各应用之间唯一的共享头文件
 *
 * ============================ 为什么要集中在这一个文件 ============================
 * Arduino 会给 .ino 里的函数**自动生成原型**，并插到合并后文件的靠前位置。
 * 若某个函数的参数/返回类型在插入点还没声明，生成出来的原型就报
 * "error: 'X' has not been declared"（本项目踩过，见 CLAUDE.md 坑 3）。
 * 把共享类型全放在这里、并在主 .ino 的**最顶部** include，类型就永远在
 * 原型的插入点之前声明完毕，这个坑不存在了。
 *
 * 配套还有两条硬规则（同样见 CLAUDE.md）：
 *   1. 任何 .ino 里的函数签名**只用内建类型或本文件里的类型**。这样即使
 *      原型插错了位置也伤不到我们 —— 所以 appOpen() 收的是下标，不是
 *      const App*。
 *   2. 所有 .ino 拼接成**同一个编译单元**，static 函数共用一个命名空间。
 *      两个文件各写一个 static void enter() 会直接重定义报错。所以应用
 *      内部函数必须带前缀（lock* / desk* / set*），这是强制要求不是风格。
 *
 * 注意还有一条拼接顺序带来的后果：**主 .ino 在最前面**，所以它看不到后面
 * 文件里的 static 变量。主 .ino 要用什么，就得在这里声明成非 static 的函数。
 */
#pragma once
#include <stdint.h>

// ============================ 图标点阵 ============================
// 所有应用图标统一 16x16、**低位在左**（U8g2 的 drawXBMP 就是这么读的，
// 见 CLAUDE.md 坑 1 和 icons.ino 开头的说明）。32 字节，进 .rodata。
//
// 在这里声明是因为应用表定义在主 .ino（排最前），而点阵定义在 icons.ino
// （排后面）—— 拼接顺序决定了主 .ino 看不见它。
#define ICON_W      16
#define ICON_H      16
#define ICON_BYTES  (ICON_W * ICON_H / 8)   // 每个图标 32 字节

extern const uint8_t ICON_LOCK[ICON_BYTES];   // 定时
extern const uint8_t ICON_NET[ICON_BYTES];    // 网络
extern const uint8_t ICON_GEAR[ICON_BYTES];   // 设置


// ============================ 应用 ============================
// 一个应用要提供的全部东西。函数指针而不是虚基类：
// APPS[] 是编译期常量表，整体落在 .rodata（Flash），零 RAM、零启动开销；
// 虚基类要为每个派生类生成 vtable 并给每个实例加一个 vptr，换来的是本项目
// 完全用不到的多态替换能力。
struct App {
  const char    *name;          // 桌面标签，UTF-8。**两个汉字**：格子 42px，
                                // "定时" 实测 23px、"网络" 24px、"设置" 23px；
                                // 三个汉字（"定时锁" 35px）也塞得下，但三个标签
                                // 长短不一，三等分的网格看上去就不齐了。
  const uint8_t *icon;          // 16x16 点阵，低位在左
  const char *(*status)(void);  // 心跳日志用的一句话状态；没有就返回 ""
  void (*enter)(void);          // 进入时一次
  void (*leave)(void);          // 离开时一次（挂起状态要在这里清干净）
  void (*tick) (uint32_t now);  // 每轮 loop：推进时间/动画/归零判定
  void (*input)(uint32_t now);  // 每轮 loop：消费按键
  void (*draw) (uint32_t now);  // 仅刷新节拍：画内容，不管 clearBuffer/sendBuffer
};

// tick 和 input 必须分开，不能把"推进时间"塞进 input：倒计时归零与按键无关，
// 必须每轮无条件跑。调用顺序沿用原来的 loop()：input → tick → draw。
//
// draw 不负责 clearBuffer / sendBuffer —— 框架在前后各做一次。这样应用不可能
// 忘记 sendBuffer，也不会出现两次 1024 字节 @400kHz ≈ 23ms 的 I2C 传输。

// 应用表：**只放能从桌面启动的应用**。桌面自己不在表里 —— 它是这张表的
// 视图，不该出现在自己的网格里。定义在主 .ino 里，这里只声明。
//
// ⚠️ 桌面是**一行三格**（用户 2026-09-22 定的版面：状态栏 16px + 图标行 32px，
// 第 4 段留空）。所以这个数**最大就是 3**：第 4 个应用得先改版面（翻页，或
// 退回两行），不是把 3 改成 4 就完事 —— 那样第 4 格会画到 y48 的空白区里，
// 而且第 1 格会被第 4 格绕回来覆盖。改之前先跑 tools/desktop_sim.py。
#define APP_COUNT 3
extern const App *const APPS[APP_COUNT];


// ============================ 框架 API ============================
void appOpen(uint8_t idx);        // 切到 APPS[idx]
void appBackToDesktop(void);      // 回桌面。**必须幂等**：已经在桌面时是空操作
bool k4TapTake(void);             // 本帧有一次"已确认的 K4 单击" → 消费掉并返回 true
uint32_t k4HeldMs(uint32_t now);  // K4 已按住多久（长按语义照旧用它）

// K4 的语义由框架整形，应用**不得**再直接读 btn[3].pressEdge / releaseEdge：
//   短按   —— 应用调 k4TapTake() 消费；不消费就当作"返回桌面"
//   双击   —— 框架直接返回桌面，应用收不到
//   长按   —— 不发 tap，应用用 k4HeldMs() 自己判
// 详细规则和理由见主 .ino 的 k4Shape()。


// ============================ 共用绘制（ui_kit.ino） ============================
// 在这里声明而不是让 ui_kit.ino 里 static：.ino 按字母序拼接，ui_kit 排最后，
// 排在它前面的 app_lock.ino 就要用这两个函数了。
void drawBand(int i, const char *s);
void drawBigTime(uint32_t totalSecs);


// ============================ 各应用 ============================
// 桌面（app_desktop.ino）
const char *deskStatus(void);
void deskEnter(void);
void deskLeave(void);
void deskTick(uint32_t now);
void deskInput(uint32_t now);
void deskDraw(uint32_t now);

// 定时锁（app_lock.ino）
const char *lockStatus(void);
bool lockIsRunning(void);     // 状态栏要用：桌面在最前面，读不到这个文件里的 static
void lockEnter(void);
void lockLeave(void);
void lockTick(uint32_t now);
void lockInput(uint32_t now);
void lockDraw(uint32_t now);

// 网络（app_net.ino）
const char *netStatus(void);
void netEnter(void);
void netLeave(void);
void netTick(uint32_t now);
void netInput(uint32_t now);
void netDraw(uint32_t now);

// 设置（app_settings.ino）
const char *setStatus(void);
void setEnter(void);
void setLeave(void);
void setTick(uint32_t now);
void setInput(uint32_t now);
void setDraw(uint32_t now);
