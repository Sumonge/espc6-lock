/*
 * espc6-lock —— 定时锁固件
 * 目标板：Seeed XIAO ESP32C6
 * 显示屏：SSD1315 128x64 OLED (I2C)
 * 执行器：舵机 + 机械拨片
 *
 * ============================ 功能 ============================
 *   纯倒计时时间锁，无密码。
 *   空闲(开锁) --K3启动--> 倒计时中(锁定) --归零--> 空闲(开锁)
 *
 *   时长设定：K1 增加 / K2 减少
 *            单击 1 分钟，长按自动加速（1 → 5 → 15 分钟/次）
 *            双击直接跳 1 小时
 *            K3 启动倒计时
 *            K4 归零重设
 *   倒计时中：K4 长按满 5 秒才能取消（防误触）
 *             按住期间画面切成"紧急解锁中"+ 羽毛向右飘的 1px 进度条
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

// ==================================================================
//  配置区 —— 需要调整的参数都在这里
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

// ---------- 行为 ----------
#define CANCEL_HOLD_MS       5000  // 倒计时中取消需要的长按时长
#define UNLOCK_NOTICE_MS     5000  // 归零/取消后提示显示多久
#define MAX_SET_MINUTES      1440  // 可设上限 = 24 小时
#define UI_REFRESH_MS          50  // 屏幕刷新周期

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
// heldMs() 单独给 K4 的"长按 5 秒取消"用。
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
//  状态机
// ==================================================================
enum State : uint8_t { ST_IDLE, ST_RUNNING };

// 倒计时归零和长按 K4 取消走的是同一条收尾路径，但屏幕文案必须分开 ——
// 自己主动取消却看到"时间到"，会让人以为按钮按错了。
//
// 这里用宏而不是 enum：Arduino 会给 .ino 里的函数自动生成原型，并插到
// 文件靠前的位置。若 enterIdle() 的签名里用了本文件后面才定义的类型，
// 生成出来的原型就会报 "has not been declared"。
#define UNLOCK_TIMEOUT   false   // 倒计时自己走完
#define UNLOCK_MANUAL    true    // 长按 K4 主动取消

static State    state              = ST_IDLE;
static uint16_t setMinutes         = 0;    // 用户设定的时长（分钟）
static uint32_t remainMs           = 0;    // 剩余毫秒
static uint32_t lastTickMs         = 0;
static uint32_t unlockNoticeEndMs  = 0;    // "已解锁" 提示显示到什么时候
static bool     unlockWasManual    = false;  // 本次是主动取消还是超时

// K1/K2 的"已按下一次，正在等第二次"状态，详见 adjustKey()。
// [0] = K1（加），[1] = K2（减）
struct ClickState {
  bool     waiting = false;
  uint32_t firstMs = 0;
};
static ClickState click[2];


static void enterIdle(uint32_t now, bool manual) {
  state             = ST_IDLE;
  remainMs          = 0;
  unlockNoticeEndMs = now + UNLOCK_NOTICE_MS;
  unlockWasManual   = manual;
  servoUnlock();
  Serial.printf("[timer] %s -> 空闲/开锁位 (本次设定 %u 分钟)\n",
                manual ? "手动取消" : "时间到", setMinutes);
}

static void startTimer(uint32_t now) {
  click[0].waiting = click[1].waiting = false;   // 丢弃没结完的单击，别带进倒计时
  if (setMinutes == 0) return;

  remainMs   = (uint32_t)setMinutes * 60000UL;
  lastTickMs = now;
  state      = ST_RUNNING;

  servoLock();
  Serial.printf("[timer] 启动倒计时 %u 分钟 -> 锁定位\n", setMinutes);
}

static void timerTick(uint32_t now) {
  if (state != ST_RUNNING) return;

  const uint32_t dt = now - lastTickMs;   // 用增量累加，天然免疫 millis() 回绕
  lastTickMs = now;

  if (dt >= remainMs) {
    remainMs = 0;
    Serial.println(F("[timer] 倒计时归零"));
    enterIdle(now, UNLOCK_TIMEOUT);
  } else {
    remainMs -= dt;
  }
}


// ==================================================================
//  界面
// ==================================================================
// 屏幕 128x64，横向四等分，每段 16px：
//   第 1 段   —— 标题／状态
//   第 2+3 段 —— 大号 HH:MM:SS，跨两行显示（合计 32px）
//   第 4 段   —— 提示（待机时留空）
//
// 例外：长按 K4 进入紧急解锁时，第 2、3 行放动画、第 4 行放进度条
// （见 drawUnlockProgress）。
//
// 中文必须用 drawUTF8()，不能用 drawStr()。
// 本文件也必须存为 UTF-8 编码（Arduino IDE 默认如此），否则中文会乱码。

#define BAND_H          16      // 每段高度
#define BAND_BASE0      12      // 第 1 段文字的基线
#define BAND_SEP_Y      15      // 唯一一条分隔线的 y 坐标
#define BIG_TIME_BASE   44      // 大号时间的基线（跨第 2、3 段）
                                // 数字偏高就把这个值调大，偏低就调小，一次 1~2

static inline int bandBaseline(int i) { return BAND_BASE0 + BAND_H * i; }

// 段位编号 0~3，从上到下
static void drawBand(int i, const char *s) {
  u8g2.setFont(CN_FONT);
  u8g2.drawUTF8(2, bandBaseline(i), s);
}

// 把秒数画成 HH:MM:SS，用大号数字字体，按实测宽度水平居中
static void drawBigTime(uint32_t totalSecs) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
           (unsigned)(totalSecs / 3600),
           (unsigned)((totalSecs / 60) % 60),
           (unsigned)(totalSecs % 60));

  u8g2.setFont(BIG_FONT);
  // 用 getStrWidth 量实际宽度再居中，这样换任何字体都不会偏
  int x = (128 - (int)u8g2.getStrWidth(buf)) / 2;
  if (x < 0) x = 0;
  u8g2.drawStr(x, BIG_TIME_BASE, buf);
}

// ---------- 紧急解锁进度条 ----------
// 长按 K4 时第 1 段换成"紧急解锁中"，底下三行分两块用：
//   第 2、3 行 —— 动画：脚丫钉在左端，羽毛随长按时长向右飘
//   第 4 行    —— 1px 条形进度条，实线是走过的部分，点线标出剩余轨道
#define EMERGENCY_MS     150    // 长按超过这么久就切到紧急解锁界面
#define FOOT_W            14    // 脚丫点阵的宽
#define FOOT_H            24    // 脚丫点阵的高
#define ANIM_BASE_Y       45    // 脚丫和羽毛共用的底线
#define FOOT_X            14    // 脚丫左边缘
#define FOOT_Y   (ANIM_BASE_Y - FOOT_H + 1)
#define FEATHER_CX0       39    // 羽轴起点：紧挨脚丫右侧
#define FEATHER_CX1      118    // 羽轴终点
#define TRACK_Y           55    // 进度条所在 y：第 4 行的中线

// 脚丫用点阵画，14x24，俯视脚印、脚趾朝上。
//
// 位序（踩过坑，别按直觉改）：U8g2 的 drawXBMP 是「低位在左」。
// 源码 src/clib/u8g2_bitmap.c 里 u8g2_DrawHXBMP() 是
//     mask = 1;  ...  mask <<= 1;
// 从 bit0 起步往左移，所以每个字节的 bit0 才是这一行最左边的像素，bit7 最右。
//    第 1 个字节 bit0~bit7  →  第 1~8  个像素
//    第 2 个字节 bit0~bit5  →  第 9~14 个像素（高 2 位空着不用）
// 按 MSB 打包会出两个错：前 8 个像素左右镜像，而第 2 个字节高位的像素
// 因为一行只数 14 位、根本读不到而被丢掉 —— 表现就是脚丫只画出左边一半。
// 每行后面的注释画的就是这一行，想改形状照着一行行的 # 和 . 改十六进制即可。
//
// 形状：左脚脚印，五个脚趾朝上 —— 大脚趾在右（x10~x11，两像素宽），
// 往左依次变小变短；底下是脚掌，第 15~18 行右边缘往里收，那就是足弓的凹陷。
// 右轮廓每行最多只挪 1 像素，拐弯处逐行渐变（11-10-9 / 9-10-11-12），
// 所以是圆弧收进去再放出来，不会出现一条长直的斜边。
// 所有墨点都收在 x1~x12 之内，左右各留 1px 空白。
static const uint8_t ICON_FOOT[48] = {
  0x40, 0x0D,  //  ......#.#.##..
  0x50, 0x0D,  //  ....#.#.#.##..
  0x54, 0x0D,  //  ..#.#.#.#.##..
  0x54, 0x0D,  //  ..#.#.#.#.##..
  0x54, 0x0D,  //  ..#.#.#.#.##..
  0x00, 0x00,  //  ..............   <- 脚趾与脚掌之间断开
  0x00, 0x00,  //  ..............
  0xF8, 0x0F,  //  ...#########..
  0xFC, 0x1F,  //  ..###########.
  0xFE, 0x1F,  //  .############.   <- 脚掌最宽处
  0xFE, 0x1F,  //  .############.
  0xFE, 0x1F,  //  .############.
  0xFC, 0x1F,  //  ..###########.
  0xFC, 0x1F,  //  ..###########.
  0xF8, 0x1F,  //  ...##########.
  0xF8, 0x0F,  //  ...#########..   <- 足弓：右边缘向内凹
  0xF0, 0x07,  //  ....#######...
  0xF0, 0x03,  //  ....######....
  0xF0, 0x03,  //  ....######....
  0xF0, 0x07,  //  ....#######...
  0xF8, 0x0F,  //  ...#########..   <- 脚跟：往外鼓回来
  0xF8, 0x07,  //  ...########...
  0xF0, 0x03,  //  ....######....
  0xE0, 0x01,  //  .....####.....
};

// 羽毛不用点阵，直接画。
// 点阵在这个尺寸下只会坨成一个实心色块，怎么描都像叶子 —— 因为羽毛的特征
// 根本不在轮廓，而在"一根轴 + 两排羽枝"这个结构。所以照结构画：
//   羽轴 —— 一条竖线
//   羽枝 —— 沿羽轴排一排斜线，长度按抛物线 4t(1-t) 分布，中间最长、两头收短
#define FEATHER_BARBS     10    // 羽枝对数
#define FEATHER_LEN       22    // 羽轴长度
#define FEATHER_MAXLEN     6    // 最长的一对羽枝
#define FEATHER_RISE       2    // 羽枝每伸长 2px 才上升 1px，压出羽毛那种斜度
#define FEATHER_LEAN      10    // 羽尖相对羽轴底端向左偏这么多像素

static void drawFeather(int cx, int baseY) {
  // 羽轴不是竖线：底端在 (cx, baseY)，斜向左上连到羽尖，整根羽毛跟着歪。
  const int tipX = cx - FEATHER_LEAN;
  const int tipY = baseY - FEATHER_LEN;
  u8g2.drawLine(cx, baseY, tipX, tipY);

  const int n = FEATHER_BARBS + 1;
  for (int i = 1; i <= FEATHER_BARBS; i++) {
    const int len = FEATHER_MAXLEN * 4 * i * (n - i) / (n * n);   // 抛物线
    if (len <= 0) continue;
    // 着生点沿倾斜的羽轴取，羽枝才会跟着羽轴一起斜，而不是挂在一根竖线上
    const int px = cx + (tipX - cx) * i / n;
    const int py = baseY + (tipY - baseY) * i / n;
    const int dy = len / FEATHER_RISE;
    u8g2.drawLine(px, py, px - len, py - dy);
    u8g2.drawLine(px, py, px + len, py - dy);
  }
}

static void drawUnlockProgress(uint32_t held) {
  unsigned pct = (unsigned)((uint64_t)held * 100 / CANCEL_HOLD_MS);
  if (pct > 100) pct = 100;

  // 第 2、3 行：脚丫钉死不动，羽毛按进度从 FEATHER_CX0 飘到 FEATHER_CX1。
  // 这里是整数 lerp：羽轴 x = 起点 + (终点 - 起点) * pct / 100，省掉浮点。
  const int featherCx = FEATHER_CX0 + (FEATHER_CX1 - FEATHER_CX0) * (int)pct / 100;
  u8g2.drawXBMP(FOOT_X, FOOT_Y, FOOT_W, FOOT_H, ICON_FOOT);
  drawFeather(featherCx, ANIM_BASE_Y);

  // 第 4 行：整幅宽的 1px 进度条
  const int fillEnd = (int)pct * 128 / 100;
  u8g2.drawHLine(0, TRACK_Y, fillEnd);           // 走过的部分：实线
  for (int x = fillEnd + 1; x < 128; x += 2) {   // 剩下的部分：点线，标出轨道总长
    u8g2.drawPixel(x, TRACK_Y);
  }
}

static void drawScreen(uint32_t now) {
  u8g2.clearBuffer();

  u8g2.drawHLine(0, BAND_SEP_Y, 128);   // 第 1 段与下方内容之间的一条分隔线

  if (state == ST_RUNNING) {
    const uint32_t held = heldMs(btn[3], now);

    if (held > EMERGENCY_MS) {
      drawBand(0, "紧急解锁中");
      drawUnlockProgress(held);
    } else {
      // 剩余秒数向上取整，避免还剩几百毫秒就显示 0 秒
      const uint32_t secsLeft = (remainMs + 999UL) / 1000UL;

      drawBand(0, "已锁定");
      drawBigTime(secsLeft);
      drawBand(3, "长按 K4 取消");
    }
  } else if ((int32_t)(unlockNoticeEndMs - now) > 0) {
    // 刚归零或刚取消，提示几秒。两种情况文案分开，别让主动取消显示成"时间到"
    drawBand(0, unlockWasManual ? "已取消" : "已解锁");
    drawBigTime(0);
    drawBand(3, unlockWasManual ? "手动取消 拨片已打开" : "时间到  拨片已打开");
  } else {
    // 待机，正在设定时长
    drawBand(0, "定时锁  待机中");
    drawBigTime((uint32_t)setMinutes * 60);
    // 第 4 段留空，待定
  }

  u8g2.sendBuffer();
}


// ==================================================================
//  输入处理
// ==================================================================
// 按住越久步进越大，避免设 2 小时要按 120 下。
// 配 120ms 的固定连发间隔，三档对应速度约为 8 / 42 / 125 分钟每秒：
//   短按，或按住 <2 秒  → 每次 1 分钟（精确调节）
//   按住 2~5 秒         → 每次 5 分钟
//   按住 >5 秒          → 每次 15 分钟
static uint16_t stepFor(uint32_t held) {
  if (held >= 5000) return 15;
  if (held >= 2000) return  5;
  return 1;
}

// ---------- 双击跳 1 小时 ----------
#define DOUBLE_PRESS_MS   300   // 第二次按下要在这个窗口内才算双击
#define DOUBLE_PRESS_STEP  60   // 双击一次跳多少分钟（1 小时）

// 这一档刻意不参与连发加速 —— 1 小时是"双击"这个离散动作换来的，
// 想调 3 小时就双击 3 次，不需要（也不应该）靠长按去加速。
// 加速只作用在分钟级的单击/连发上（见 stepFor）。

static uint16_t clampSet(int32_t v) {
  if (v < 0)                 return 0;
  if (v > MAX_SET_MINUTES)   return MAX_SET_MINUTES;
  return (uint16_t)v;
}

// K1 加 / K2 减，sign 取 +1 或 -1。
//
// 单击不能立刻生效：双击的第一下如果当场按 1 分钟算掉，双击就成了
// "+1 分钟 再 +1 小时"。所以第一次按下先挂起，等 DOUBLE_PRESS_MS 过去
// 没有第二次按下，才补上这次单击（按住的话按当时的档位给连发）。
static void adjustKey(int i, int8_t sign, uint32_t now) {
  const Button &b = btn[i];
  ClickState   &c = click[i];

  if (b.pressEdge) {
    if (c.waiting && (uint32_t)(now - c.firstMs) <= DOUBLE_PRESS_MS) {
      c.waiting = false;            // 双击：丢掉挂起的那次单击，直接跳 1 小时
      setMinutes = clampSet((int32_t)setMinutes + sign * DOUBLE_PRESS_STEP);
    } else {
      c.waiting = true;             // 第一次按下，挂起等第二次
      c.firstMs = now;
    }
    return;
  }

  if (c.waiting) {
    if ((uint32_t)(now - c.firstMs) <= DOUBLE_PRESS_MS) return;   // 窗口内，连发也先压住
    c.waiting = false;              // 窗口过去了，补上这次单击
    setMinutes = clampSet((int32_t)setMinutes + sign * (int32_t)stepFor(heldMs(b, now)));
    return;
  }

  if (b.repeatEdge) {               // 一直按着 → 连发
    setMinutes = clampSet((int32_t)setMinutes + sign * (int32_t)stepFor(heldMs(b, now)));
  }
}

static void handleInput(uint32_t now) {
  if (state == ST_IDLE) {
    adjustKey(0, +1, now);                      // K1 增加时长
    adjustKey(1, -1, now);                      // K2 减少时长

    if (btn[2].pressEdge) startTimer(now);      // K3 启动

    if (btn[3].pressEdge) {                     // K4 归零重设
      setMinutes = 0;
      unlockNoticeEndMs = 0;
      click[0].waiting = click[1].waiting = false;
    }
  } else {                                      // ST_RUNNING
    // 只有 K4 长按满 CANCEL_HOLD_MS 才能中断。
    // 注意这里刻意不用 pressEdge，否则一碰就取消了。
    if (heldMs(btn[3], now) >= CANCEL_HOLD_MS) {
      enterIdle(now, UNLOCK_MANUAL);
    }
  }
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

  // ---------- 舵机 ----------
  servoInit();
  servoUnlock();   // 上电即开锁位 —— 与"不保存计时状态"的设计一致
  Serial.printf("[servo] GPIO%d 初始化完成，上电位于开锁位 (%uus)\n",
                PIN_SERVO, SERVO_US_UNLOCK);

  Serial.println(F("----------------------------------------"));
  Serial.println(F("[ui  ] K1 加 / K2 减 / K3 启动 / K4 归零"));
  Serial.println(F("[ui  ] 单击 1 分钟，长按加速，双击跳 1 小时"));
  Serial.println(F("[ui  ] 倒计时中长按 K4 满 5 秒可取消"));
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

  handleInput(now);
  timerTick(now);

  static uint32_t lastDrawMs = 0;
  if (now - lastDrawMs >= UI_REFRESH_MS) {
    lastDrawMs = now;
    drawScreen(now);
  }

  // 心跳日志：每 5 秒一行，用来确认固件真的在跑。
  // 调试期非常有用 —— 没有它的话，不按键时串口完全静默，
  // 看起来就像"板子没反应"，会把"固件没跑"和"固件在跑但没输出"混为一谈。
  // 稳定之后可以把这个 if 整块删掉。
  static uint32_t lastBeatMs = 0;
  if (now - lastBeatMs >= 5000) {
    lastBeatMs = now;
    Serial.printf("[beat] %s  up=%lus  K=[%d%d%d%d]\n",
                  (state == ST_RUNNING) ? "RUNNING" : "IDLE  ",
                  (unsigned long)(now / 1000),
                  btn[0].level, btn[1].level, btn[2].level, btn[3].level);
  }
}
