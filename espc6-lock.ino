/*
 * espc6-lock —— 定时锁固件（主文件）
 * 目标板：Seeed XIAO ESP32C6
 * 显示屏：SSD1315 128x64 OLED (I2C)
 * 执行器：舵机 + 机械拨片
 *
 * ============================ 结构 ============================
 * 这个文件是**框架**：硬件初始化、按键、K4 整形、启动画面、应用到绘制的
 * 分发、setup/loop。具体功能在各自的应用文件里：
 *
 *   app.h            框架与应用之间唯一的共享头（改之前先读它的开头注释）
 *   app_desktop.ino  桌面：3x2 图标网格
 *   app_lock.ino     定时锁应用
 *   app_settings.ino 设置应用
 *   icons.ino        图标点阵
 *   ui_kit.ino       共用绘制工具（段版面、大号文字）
 *
 * 拼接顺序是「主文件在最前，其余按字母序」，所以后面那些文件能看到这里的
 * 一切，反过来不行 —— 主文件要用后面文件里的东西，必须在 app.h 里声明。
 *
 * ============================ 功能 ============================
 *   老式功能机那样的桌面：一张 3x2 图标网格，用 K1/K2 选、K3 进、K4 返回。
 *   现在网格里有两个应用：
 *
 *   定时锁 —— 纯倒计时时间锁，无密码。
 *   空闲(开锁) --K3启动--> 倒计时中(锁定) --归零--> 空闲(开锁)
 *
 *     时长设定：K1 增加 / K2 减少
 *              单击 1 分钟，长按自动加速（1 → 5 → 15 分钟/次）
 *              双击直接跳 1 小时
 *              K3 启动倒计时
 *              K4 归零重设
 *     倒计时中：K4 长按满 5 秒才能取消（防误触）
 *               按住期间画面切成"紧急解锁中"+ 羽毛向右飘的进度条
 *
 *   设置 —— 目前只有「关于」页。
 *
 *   !! 设计前提：计时状态不保存 !!
 *   上电后一律回到「空闲 = 开锁位」。也就是说拔电重启即可绕过锁定，
 *   本设备不是安全设备，是定时器。舵机因此是 fail-open 的。
 *
 * ============================ 依赖库 ============================
 *   U8g2  by Oliver Kraus（Arduino IDE 库管理器搜索 "U8g2"）
 *   舵机用板载 LEDC 直接产生 PWM，不需要额外库。
 *
 * ============================ 接线 ============================
 *   屏幕 GND  → XIAO GND
 *   屏幕 VCC  → XIAO 3V3        <-- 必须 3.3V
 *   屏幕 SCL  → XIAO D5   (GPIO 23)
 *   屏幕 SDA  → XIAO D4   (GPIO 22)
 *   屏幕 K1   → XIAO D0   (GPIO 0)
 *   屏幕 K2   → XIAO D1   (GPIO 1)
 *   屏幕 K3   → XIAO D2   (GPIO 2)
 *   屏幕 K4   → XIAO D3   (GPIO 21)
 *   舵机 信号 → XIAO D8   (GPIO 19)
 *   舵机 VCC  → 外部 5V 电源（切勿接 XIAO 的 5V 脚）
 *   舵机 GND  → 外部电源 GND，并与 XIAO GND 共地
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "app.h"      // 必须在最顶上：自动生成的函数原型要能看见这里的类型

// ==================================================================
//  配置区 —— 需要调整的参数都在这里
//  各应用自己的参数在各自文件顶部的配置区（例：app_lock.ino）
// ==================================================================

// ---------- 引脚 ----------
#define PIN_I2C_SDA       22    // 屏幕 SDA → XIAO D4
#define PIN_I2C_SCL       23    // 屏幕 SCL → XIAO D5
#define PIN_BTN1           0    // 屏幕 K1  → XIAO D0
#define PIN_BTN2           1    // 屏幕 K2  → XIAO D1
#define PIN_BTN3           2    // 屏幕 K3  → XIAO D2
#define PIN_BTN4          21    // 屏幕 K4  → XIAO D3
#define PIN_SERVO         19    // 舵机信号 → XIAO D8

static const uint8_t BTN_PINS[4]  = { PIN_BTN1, PIN_BTN2, PIN_BTN3, PIN_BTN4 };
static const char*   BTN_NAMES[4] = { "K1", "K2", "K3", "K4" };

// ---------- 舵机 ----------
// 用 LEDC 产生 50Hz PWM。脉宽换算成占空比在 servoWriteUs() 里做。
#define SERVO_FREQ_HZ     50
#define SERVO_RES_BITS    14    // 14 位分辨率 → 20000us/16384 ≈ 1.22us 步进
#define SERVO_CHANNEL      0    // 仅 Arduino-ESP32 2.x 需要

// 两个位置的脉宽（微秒）。不同舵机行程不同，装好后按实际需要微调：
//   数值调大 → 往一个方向多转，调小 → 往反方向多转
//   如果舵机在某个位置发出持续的"吱吱"声或发热，说明它顶到机械限位了，
//   把对应数值往中间调 50~100us。
#define SERVO_US_UNLOCK  1000   // 开锁位
#define SERVO_US_LOCK    2000   // 锁定位

// ---------- 按键时序 ----------
#define BTN_DEBOUNCE_MS      20    // 消抖窗口
#define BTN_REPEAT_DELAY_MS  400   // 按住多久后开始连发
#define BTN_REPEAT_MS        120   // 连发间隔（固定值，不随按住时长变化）
//
// 连发间隔刻意做成固定的，只让"每次跳几分钟"随按住时长变化。
// 曾经让间隔和步进同时加速，两者是相乘关系，导致按住 3 秒后速度
// 骤增 18 倍，17 分钟以上的值根本停不下来。只能有一个因子变。

// ---------- K4 整形 ----------
// K4 在应用里兼两个语义：短按给应用用，双击是"返回桌面"。为了分清，
// 单击必须挂起等一个双击窗口 —— 理由和 adjustKey 完全一样：双击的第一下
// 如果当场算掉，"双击返回"就会先触发一次单击的副作用。
//
// 这与"归零重设"交互的取舍是用户确认过的：归零晚 ≤250ms 生效，可接受。
#define K4_DOUBLE_MS         300   // 第二次按下要在这个窗口内才算双击
#define K4_TAP_MAX_MS        800   // 按住超过这么久就不算单击（是长按，不发 tap）
//
// K4_TAP_MAX_MS 不是可选的润色，是必须的：倒计时中"长按 5 秒取消"松手时，
// 如果没有这道闸，会补发一次单击，而锁应用在空闲态把单击当"归零重设"——
// 于是"取消完顺手把设定值清成 0"。症状很隐蔽，只有从倒计时里取消才看得到。

// ---------- 框架 ----------
#define UI_REFRESH_MS          50  // 屏幕刷新周期
#define STARTUP_MS           1500  // 启动画面停留多久后进桌面

// 版本号：只出现在启动画面和「设置 → 关于」页上。
// 它**不是**烧录与否的证据 —— 忘了改版本号太常见了，判断烧没烧进去要看
// 关于页的 __DATE__（编译时刻）和固件字节数。
#define FW_VERSION           "1.0"

// ---------- 显示 ----------
// 中文字体：U8g2 的文泉驿点阵字体，必须配合 drawUTF8() 使用（drawStr 不行）。
//
// 本机 U8g2 2.36.19 已确认存在下列名字（要查别的，在
// U8g2/src/clib/u8g2.h 里搜 "wqy"，字体名都在那里声明）：
//   u8g2_font_wqy12_t_chinese1     9 KB   常用字，最省 Flash
//   u8g2_font_wqy12_t_chinese3    14 KB   收字更多
//   u8g2_font_wqy12_t_gb2312     198 KB   完整 GB2312
//
// 本项目屏幕上实际只出现二十来个汉字，用 gb2312 属于大炮打蚊子；
// 但 ESP32-C6 有 4MB Flash，198KB 放得下，所以先留着 ——
// 没必要为了省空间去赌某个字不在精简字库里。
//
// 大字体在库源码里被 #ifdef U8G2_USE_LARGE_FONTS 包着，不过 ESP32 上
// u8g2.h 会按 ESP_PLATFORM 自动打开这个开关，不需要自己 #define。
#define CN_FONT   u8g2_font_wqy12_t_gb2312

// 大号时间字体：跨第 2、3 段显示 HH:MM:SS。
// logisoso 是 U8g2 自带的数字字体，_tn 后缀表示"只含数字和 . : - 空格"，
// 正好够画时间。想换大小就改数字：logisoso20_tn / _28_tn / _32_tn。
// 保底备选：u8g2_font_10x20_tf（等宽 10px，一定能用，但明显小一圈）
#define BIG_FONT  u8g2_font_logisoso24_tn

// ---------- 版面 ----------
// 屏幕横向四等分，每段 16px。这几个尺寸放在这里而不是 ui_kit.ino，是因为
// .ino 按字母序拼接、ui_kit 排在最后，而 app_lock.ino 要用 BAND_SEP_Y ——
// 宏是文本替换，定义点必须在用处之前。
#define BAND_H          16      // 每段高度
#define BAND_BASE0      12      // 第 1 段文字的基线
#define BAND_SEP_Y      15      // 唯一一条分隔线的 y 坐标
#define BIG_TIME_BASE   44      // 大号文字的基线（跨第 2、3 段）
                                // 数字偏高就把这个值调大，偏低就调小，一次 1~2

// ==================================================================
//  显示屏对象
// ==================================================================
// SSD1315 与 SSD1306 命令集兼容，直接套用 SSD1306 驱动。
// 第 3、4 个参数是 SCL / SDA，必须显式传入 —— 本项目 I2C 用的是
// D4/D5 而非芯片默认 I2C 引脚，留空会导致屏幕完全不亮。
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
    U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);


// ==================================================================
//  按键
// ==================================================================
// 按下接地、内部上拉、低电平有效。
// 每个按键对外提供三种事件（每次 buttonsUpdate 后有效）：
//   pressEdge   —— 消抖后"刚按下"那一次
//   repeatEdge  —— 按住不放时周期性触发（带加速，用于快速加减）
//   releaseEdge —— 松开那一次
// heldMs() 单独给"长按"语义用。
//
// 注意 K4 例外：应用**不得**直接读 btn[3].pressEdge / releaseEdge，
// 必须走 k4TapTake() / k4HeldMs()，理由见 K4 配置区。
struct Button {
  uint8_t  pin;
  bool     raw          = false;  // 原始电平（已折算成"按下 = true"）
  bool     level        = false;  // 消抖后的稳定电平
  bool     pressEdge    = false;
  bool     releaseEdge  = false;
  bool     repeatEdge   = false;
  uint32_t rawChangeMs  = 0;      // 原始电平上次跳变的时刻
  uint32_t downMs       = 0;      // 本次按下的时刻
  uint32_t nextRepeatMs = 0;
};

static Button btn[4];

static void buttonsInit() {
  for (int i = 0; i < 4; i++) {
    btn[i].pin = BTN_PINS[i];
    pinMode(btn[i].pin, INPUT_PULLUP);
  }
}

static void buttonsUpdate(uint32_t now) {
  for (int i = 0; i < 4; i++) {
    Button &b = btn[i];
    b.pressEdge = b.releaseEdge = b.repeatEdge = false;

    const bool raw = (digitalRead(b.pin) == LOW);   // 低电平 = 按下

    if (raw != b.raw) {          // 原始电平跳变，重新开始消抖计时
      b.raw = raw;
      b.rawChangeMs = now;
    }
    if (now - b.rawChangeMs < BTN_DEBOUNCE_MS) continue;  // 还在抖动窗口内，不采信

    if (b.level == b.raw) {      // 电平稳定且未变化
      if (b.level && now >= b.nextRepeatMs) {             // 一直按着 → 连发
        b.repeatEdge = true;
        b.nextRepeatMs = now + BTN_REPEAT_MS;             // 固定间隔，不加速
      }
      continue;
    }

    b.level = b.raw;             // 采纳新电平
    if (b.level) {
      b.downMs       = now;
      b.pressEdge    = true;
      b.nextRepeatMs = now + BTN_REPEAT_DELAY_MS;
    } else {
      b.releaseEdge  = true;
    }
  }
}

static uint32_t heldMs(const Button &b, uint32_t now) {
  return b.level ? (now - b.downMs) : 0;
}

uint32_t k4HeldMs(uint32_t now) { return heldMs(btn[3], now); }


// ==================================================================
//  舵机
// ==================================================================
static void servoInit() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_SERVO, SERVO_FREQ_HZ, SERVO_RES_BITS);
#else
  ledcSetup(SERVO_CHANNEL, SERVO_FREQ_HZ, SERVO_RES_BITS);
  ledcAttachPin(PIN_SERVO, SERVO_CHANNEL);
#endif
}

// 把脉宽（微秒）换算成 LEDC 占空比并输出。
// LEDC 会持续输出，所以舵机会一直保持在指定位置（有保持力）。
static void servoWriteUs(uint16_t us) {
  const uint32_t periodUs = 1000000UL / SERVO_FREQ_HZ;
  const uint32_t dutyMax  = (1UL << SERVO_RES_BITS) - 1;
  uint32_t duty = (uint32_t)us * dutyMax / periodUs;

  if (duty > dutyMax) duty = dutyMax;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_SERVO, duty);
#else
  ledcWrite(SERVO_CHANNEL, duty);
#endif
}

static inline void servoUnlock() { servoWriteUs(SERVO_US_UNLOCK); }
static inline void servoLock()   { servoWriteUs(SERVO_US_LOCK);   }


// ==================================================================
//  应用表
// ==================================================================
// 加一个应用 = 在这里加一行 + 在 app.h 里声明它的六个函数 + 改 APP_COUNT。
//
// 顺序就是桌面网格里的顺序（按行优先铺）。定时锁放第一格：它是这块板子
// 存在的理由，桌面光标也默认停在它上面。
static const App APP_LOCK = {
  "定时锁", ICON_LOCK, lockStatus,
  lockEnter, lockLeave, lockTick, lockInput, lockDraw,
};

static const App APP_SETTINGS = {
  "设置", ICON_GEAR, setStatus,
  setEnter, setLeave, setTick, setInput, setDraw,
};

const App *const APPS[APP_COUNT] = { &APP_LOCK, &APP_SETTINGS };

// 桌面**不在 APPS[] 里**：它不该出现在自己的网格中。它是那张表的视图，
// 不是表里的一项。它的 icon 字段永远不会被画（桌面不画自己），但也不能
// 留空 —— 留个空指针等于给未来的自己埋一颗雷。
static const App APP_DESKTOP = {
  "桌面", ICON_LOCK, deskStatus,
  deskEnter, deskLeave, deskTick, deskInput, deskDraw,
};

// 永不为空（appOpen 之后）。用指针而不是下标，"桌面"才能和 APPS[] 里
// 那些应用平起平坐 —— 框架不需要知道桌面是特殊的。
static const App *curApp = nullptr;

// 所有切换都走这里，好处是"已经在里面就不动"这条幂等规则只有一处实现。
// appBackToDesktop() 的幂等性就靠它 —— 用户连续按两次返回不能有副作用。
static void appSwitch(const App *a) {
  if (curApp == a) return;
  if (curApp) curApp->leave();
  curApp = a;
  curApp->enter();
  Serial.printf("[app ] 进入 %s\n", curApp->name);
}

void appOpen(uint8_t idx) {
  if (idx >= APP_COUNT) return;
  appSwitch(APPS[idx]);
}

void appBackToDesktop(void) { appSwitch(&APP_DESKTOP); }


// ==================================================================
//  K4 整形
// ==================================================================
// K4 一个键要担三个语义，必须先把原始电平"整形"成互不冲突的事件，
// 应用才可能用得对：
//
//   短按  —— 挂起一个双击窗口后确认，应用用 k4TapTake() 取走
//   双击  —— 框架直接返回桌面，应用根本收不到
//   长按  —— 不发 tap，应用用 k4HeldMs() 自己判（倒计时中取消就是这么做的）
//
// 应用不消费这次短按，框架就当"返回桌面"。好处是设置应用一行返回代码都
// 不用写，将来新应用忘了写也是良性失败（退回桌面，而不是卡死）。
//
// 状态机四步：
//   1. 按下时若距上次按下在 K4_DOUBLE_MS 内 → 双击，作废挂起的那次
//   2. 否则挂起，开始计时
//   3. 挂起中：窗口没过就等；窗口过了且已松手 → 补发一次 tap；
//              窗口过了还按着且超过 K4_TAP_MAX_MS → 是长按，不发 tap
//   4. 双击后要吞掉这次按下直到松手，否则松手会被当成一次单击
static bool     k4TapPending   = false;  // 本帧有一次已确认的单击，等应用来取
static bool     k4Holding      = false;  // 有一次按下正挂着等双击窗口
static uint32_t k4HoldSinceMs  = 0;
static bool     k4HaveLast     = false;  // k4LastPressMs 是否有效
static uint32_t k4LastPressMs  = 0;
static bool     k4Swallow      = false;  // 双击已判出，吞掉这次按下直到松手

bool k4TapTake(void) {
  if (!k4TapPending) return false;
  k4TapPending = false;
  return true;
}

static void k4Shape(uint32_t now) {
  const Button &b = btn[3];

  // ---- 1/2. 按下 ----
  if (b.pressEdge) {
    if (k4Swallow) return;                       // 正在吞，理论到不了这（松手才清）

    if (k4HaveLast && (uint32_t)(now - k4LastPressMs) <= K4_DOUBLE_MS) {
      k4HaveLast = false;
      k4Holding  = false;                        // 挂起的那次单击作废
      k4Swallow  = true;                         // 吞掉这次按下，等松手
      appBackToDesktop();
      return;
    }

    k4HaveLast    = true;
    k4LastPressMs = now;
    k4Holding     = true;
    k4HoldSinceMs = now;
    return;                                      // 注意：这一帧不发 tap
  }

  // ---- 3. 挂着等窗口 ----
  if (k4Holding) {
    if ((uint32_t)(now - k4HoldSinceMs) <= K4_DOUBLE_MS) return;   // 窗口内，继续等

    if (!b.level) {                    // 已松手 → 补发一次单击
      k4Holding    = false;
      k4TapPending = true;
      return;
    }
    if (heldMs(b, now) < K4_TAP_MAX_MS) return;   // 还按着但不够久，再等等
    k4Holding = false;                            // 长按，不发 tap
  }

  // ---- 4. 松手清吞标志 ----
  if (k4Swallow && !b.level) k4Swallow = false;
}


// ==================================================================
//  启动画面
// ==================================================================
// 开机先显示一两秒再进桌面。这是**框架层**的东西，不是某个应用的一屏 ——
// 所以它不占 curApp（那会让"当前应用"在开机瞬间变成一个不存在的东西），
// 而是单独一个标志。
//
// 这段时间里按键不进应用：用户完全可能在上电的同一瞬间就在按 K3。
static bool     startupOn   = true;
static uint32_t startupAtMs = 0;

static void drawSplash(void) {
  u8g2.drawXBMP((128 - ICON_W) / 2, 10, ICON_W, ICON_H, ICON_LOCK);

  // 标题用中文字体。**不能**用 BIG_FONT —— 那是 logisoso24_tn，_tn 后缀
  // 表示只含数字和 " .:-"，画字母会得到一片空白。
  // 含中文的串必须 getUTF8Width（见 CLAUDE.md 坑 7）。标题真宽 64，居中
  // 起点 x=32。
  u8g2.setFont(CN_FONT);
  const char *title = "KHW 定时锁";
  u8g2.drawUTF8((128 - (int)u8g2.getUTF8Width(title)) / 2, 42, title);

  // 编译时刻。和「设置 → 关于」页那一行是同一个来源：两边对不上就说明
  // 屏幕上这块板子跑的不是这次编译出来的固件。
  // 这一行是纯 ASCII，两种量宽结果一样（utf8_next 对 ASCII 和 ascii_next
  // 行为相同）；统一用 getUTF8Width 是为了让"配 drawUTF8 就用 UTF8Width"
  // 这条规则没有例外，不必每次去判断串里有没有汉字。
  char buf[32];
  snprintf(buf, sizeof(buf), "v%s  %s", FW_VERSION, __DATE__);
  u8g2.drawUTF8((128 - (int)u8g2.getUTF8Width(buf)) / 2, 57, buf);
}


// ==================================================================
//  绘制分发
// ==================================================================
// clearBuffer / sendBuffer 由框架负责，应用只画内容 —— 这样应用不可能忘记
// 提交，也不会出现两次 1024 字节 @400kHz ≈ 23ms 的 I2C 传输。
static void drawFrame(uint32_t now) {
  u8g2.clearBuffer();
  if (startupOn)        drawSplash();
  else if (curApp)      curApp->draw(now);
  u8g2.sendBuffer();
}


// ==================================================================
//  setup
// ==================================================================
void setup() {
  Serial.begin(115200);
  delay(1500);   // 等 USB CDC 枚举完成，否则最开头的日志会丢

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  XIAO ESP32C6  定时锁"));
  Serial.println(F("========================================"));
  Serial.println(F("[boot] 计时状态不保存：上电一律回到空闲/开锁位"));

  // ---------- 按键体检 ----------
  // 配置成 INPUT_PULLUP 后先读一次空闲电平。没按时必须是 HIGH；
  // 若某个引脚开机就是 LOW，说明它被短路到 GND，多半是接线问题。
  buttonsInit();
  delay(10);   // 等上拉稳定
  bool pinFault = false;
  for (int i = 0; i < 4; i++) {
    const bool idleHigh = digitalRead(BTN_PINS[i]);
    Serial.printf("[btn ] %s GPIO%-2d 空闲电平 %s", BTN_NAMES[i], BTN_PINS[i],
                  idleHigh ? "HIGH  正常" : "LOW   异常");
    if (!idleHigh) { Serial.print(F("  <- 检查是否短路到 GND")); pinFault = true; }
    Serial.println();
  }
  if (pinFault) Serial.println(F("[btn ] !! 有按键引脚空闲为低，接线可能有问题"));

  // ---------- I2C + 屏幕 ----------
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);   // SSD1315 支持 400kHz

  // 地址写死 0x3C —— 刻意不做 I2C 探测。
  //
  // 原因：实测这套硬件上 Wire.endTransmission() 会给出"假 NACK"，
  // 探测说 0x3C 无应答，但 U8g2 照样能把画面写进同一块屏幕（屏幕确实亮）。
  // 之前就是因为信了这个假阴性，自动切到了根本不存在的 0x3D，导致全黑。
  //
  // 结论：屏幕在不在，以"画面上不上得来"为准，不要靠探测决定地址。
  // 若换用 0x3D 的模块，把下面这行的注释打开即可。
  // u8g2.setI2CAddress(0x3D << 1);   // setI2CAddress 收的是左移一位后的值
  u8g2.begin();
  Serial.println(F("[oled] U8g2 初始化完成 @400000Hz (地址固定 0x3C)"));

  // 字体实测量宽。drawBand() 从 x=2 起画，所以一行文字最多 126px 可用；
  // 版面估算全靠这两个数（汉字宽 / ASCII 宽），别凭字号猜。
  //
  // 每个数都用**和绘图时同一个**量宽函数：汉字走 getUTF8Width，ASCII 走
  // getStrWidth。这里若图省事都给 getStrWidth，汉字那一栏会打出 0 —— 因为
  // "字"被当成三个独立字节去查字形，查不到就返回 0（CLAUDE.md 坑 7）。
  // 一个报错的探针比没有探针更坏：它会让人以为"汉字宽是 0，版面很宽松"。
  //
  // 主机的参考值（tools/fontcheck，与固件同一份 u8g2_font.c 编出来的）：
  //     汉字 字=11   ASCII 0=5   行高=13   ascent=8  descent=-2
  // 板上打出来和这行对不上，就说明字体宏或 u8g2 版本被动过了。
  u8g2.setFont(CN_FONT);
  Serial.printf("[font] 汉字宽=%u  ASCII宽=%u  行高=%u\n",
                (unsigned)u8g2.getUTF8Width("字"),
                (unsigned)u8g2.getStrWidth("0"),
                (unsigned)u8g2.getMaxCharHeight());

  // ---------- 舵机 ----------
  servoInit();
  servoUnlock();   // 上电即开锁位 —— 与"不保存计时状态"的设计一致
  Serial.printf("[servo] GPIO%d 初始化完成，上电位于开锁位 (%uus)\n",
                PIN_SERVO, SERVO_US_UNLOCK);

  // ---------- 应用 ----------
  appBackToDesktop();        // 开机落在桌面，启动画面结束后就能用
  startupAtMs = millis();

  Serial.println(F("----------------------------------------"));
  Serial.println(F("[ui  ] 桌面：K1/K2 选  K3 进入  K4 返回"));
  Serial.println(F("[ui  ] 定时锁：K1 加 / K2 减 / K3 启动 / K4 归零"));
  Serial.println(F("[ui  ]           单击 1 分钟，长按加速，双击跳 1 小时"));
  Serial.println(F("[ui  ]           倒计时中长按 K4 满 5 秒可取消"));
  Serial.println(F("----------------------------------------"));
}


// ==================================================================
//  loop
// ==================================================================
void loop() {
  const uint32_t now = millis();

  buttonsUpdate(now);

  // 按键日志：只在状态变化时打，方便把"按键没反应"和"逻辑没写对"区分开
  for (int i = 0; i < 4; i++) {
    if (btn[i].pressEdge) {
      Serial.printf("[btn ] %s 按下\n", BTN_NAMES[i]);
    } else if (btn[i].releaseEdge) {
      Serial.printf("[btn ] %s 松开 (按住 %lu ms)\n",
                    BTN_NAMES[i], (unsigned long)(now - btn[i].downMs));
    }
  }

  // K4 必须先整形，再交给应用 —— 顺序反了应用就会看到未处理的原始边沿
  k4Shape(now);

  // 启动画面到点就散场。判断放在输入分发之前，这一帧就能落到桌面上。
  if (startupOn && (uint32_t)(now - startupAtMs) >= STARTUP_MS) {
    startupOn = false;
    Serial.println(F("[boot] 启动画面结束，进入桌面"));
  }

  if (startupOn) {
    // 启动画面期间按键不进应用 —— 用户完全可能在上电的同一瞬间就在按 K3。
    // 这里把整形器已经攒下的单击一并丢掉，免得这一帧攒的 tap 在画面
    // 结束后的第一帧补发出去。
    k4TapPending = false;
  } else if (curApp) {
    curApp->input(now);

    // 应用没把这次单击取走，就当作"返回桌面"。
    // 这条规则让"返回"永远不会失效：将来新应用忘了处理 K4 也不会把用户困住。
    if (k4TapPending) {
      k4TapPending = false;
      appBackToDesktop();
    }

    // tick 必须无条件跑：倒计时归零跟按键无关，应用在后台时也得走
    curApp->tick(now);
  }

  static uint32_t lastDrawMs = 0;
  if (now - lastDrawMs >= UI_REFRESH_MS) {
    lastDrawMs = now;
    drawFrame(now);
  }

  // 心跳日志：每 5 秒一行，用来确认固件真的在跑。
  // 调试期非常有用 —— 没有它的话，不按键时串口完全静默，
  // 看起来就像"板子没反应"，会把"固件没跑"和"固件在跑但没输出"混为一谈。
  // 稳定之后可以把这个 if 整块删掉。
  static uint32_t lastBeatMs = 0;
  if (now - lastBeatMs >= 5000) {
    lastBeatMs = now;
    Serial.printf("[beat] %s %s  up=%lus  K=[%d%d%d%d]\n",
                  curApp ? curApp->name : "----",
                  (curApp && curApp->status) ? curApp->status() : "",
                  (unsigned long)(now / 1000),
                  btn[0].level, btn[1].level, btn[2].level, btn[3].level);
  }
}
