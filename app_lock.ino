/*
 * app_lock.ino —— 定时锁应用
 *
 * 纯倒计时时间锁，无密码。
 *   空闲(开锁) --K3启动--> 倒计时中(锁定) --归零--> 空闲(开锁)
 *
 *   时长设定：K1 增加 / K2 减少
 *            单击 1 分钟，长按自动加速（1 → 5 → 15 分钟/次）
 *            双击直接跳 1 小时
 *            K3 启动倒计时
 *            K4 归零重设
 *   倒计时中：K4 长按满 5 秒才能取消（防误触）
 *             按住期间画面切成"紧急解锁中"+ 羽毛向右飘的进度条
 *
 *   !! 设计前提：计时状态不保存 !!
 *   上电后一律回到「空闲 = 开锁位」。也就是说拔电重启即可绕过锁定，
 *   本设备不是安全设备，是定时器。舵机因此是 fail-open 的。
 *
 * 这个文件里的状态（state / setMinutes / remainMs / click[] 等）全都是 static，
 * 也就是**只属于本应用**。切出去再切回来不会串台 —— 前提是 lockLeave()
 * 把挂起状态清干净，见那里的注释。
 */

// ============================ 配置区 ============================
// 本应用自己的可调参数。硬件相关的（引脚、舵机、消抖）在主 .ino 的配置区，
// 框架相关的（K4 整形、刷新周期）也在那边。
#define CANCEL_HOLD_MS       5000  // 倒计时中取消需要的长按时长
#define UNLOCK_NOTICE_MS     5000  // 归零/取消后提示显示多久
#define MAX_SET_MINUTES      1440  // 可设上限 = 24 小时

// ---------- 双击跳 1 小时 ----------
#define DOUBLE_PRESS_MS   300   // 第二次按下要在这个窗口内才算双击
#define DOUBLE_PRESS_STEP  60   // 双击一次跳多少分钟（1 小时）
//
// 这一档刻意不参与连发加速 —— 1 小时是"双击"这个离散动作换来的，
// 想调 3 小时就双击 3 次，不需要（也不应该）靠长按去加速。
// 加速只作用在分钟级的单击/连发上（见 stepFor）。

// ---------- 紧急解锁界面 ----------
// 长按 K4 时第 1 段换成"紧急解锁中"，底下三行分两块用：
//   第 2、3 行 —— 动画：脚丫钉在左端，羽毛随长按时长向右飘
//   第 4 行    —— 胶囊进度条，空轨道描边、走过的部分实心（见 drawTrackCapsule）
#define EMERGENCY_MS     150    // 长按超过这么久就切到紧急解锁界面
#define FOOT_W            14    // 脚丫点阵的宽
#define FOOT_H            24    // 脚丫点阵的高
#define ANIM_BASE_Y       45    // 脚丫和羽毛共用的底线
#define FOOT_X            14    // 脚丫左边缘
#define FOOT_Y   (ANIM_BASE_Y - FOOT_H + 1)
#define FEATHER_CX0       39    // 羽轴起点：紧挨脚丫右侧
#define FEATHER_CX1      118    // 羽轴终点

// 第 4 行的胶囊进度条。空轨道是描边，走过的部分实心填充，填充比轨道
// 内壁再缩进 1px，两者不粘连。
//
// 高度 = 2*BAR_R + 1，由圆角半径定 —— 嫌细就把 BAR_R 调大，其余尺寸都是
// 推出来的、跟着一起变：BAR_R 5 → 11px 高，6 → 13px 高。
//
// 从外到内三层的半径，每层之间差 1px 就是那一圈空隙：
//   BAR_R       轨道外壁       5
//   BAR_R - 1   轨道内壁       4   ← 中间那 1px 是描边的线宽
//   BAR_R - 2   填充端头       3   ← 再让出的 1px 是轨道与填充的空隙
#define BAR_X        2                  // 左边缘（右边留 2px 对称）
#define BAR_W      124                  // 总宽：x 2..125
#define BAR_R        5                  // 端部圆角半径
#define BAR_CY      55                  // 中心 y：第 4 行是 48..63，中线 55.5
#define BAR_PAD      2                  // 填充相对外壁的内缩量
#define BAR_RF  (BAR_R - BAR_PAD)       // 填充端头的圆角半径

// ---------- 第 2、3 段的开锁动画 ----------
// 归零/取消后的提示画面里，锁体固定不动，锁梁（半圆 + 两条腿）绕左腿
// 根部向左转 90°，最后躺成水平、弧顶指向左边。
//
// 转轴选在左腿根部、而不是锁梁正中，是因为真锁就是这么开的：左腿始终
// 插在锁体里当铰链。所以它转到底时会横躺在锁体内部、并从左壁穿出 2px。
// **这不是画错了**，是"铰链没拔出来"的必然结果——想消掉这个穿模，只能
// 把转轴挪到锁体外，那样锁梁和锁体就不再是"同一个刚体上的两截"了。
//
// 90° 是用户指定的动作（"锁头打开，向左水平翻转"），**不是一个可调的
// 风格参数**：只有整 90° 才让锁梁真正躺平（水平）且指向左。小一点不叫
// 水平，大一点就转到锁体下面去了。要改观感请调 LOCK_ANIM_MS，别动这个。
//
// 逐帧扫过 0~1400ms（10ms 一档）验过：0 处越界、0 处顶到 y=15 的分隔线、
// 0 处侵入第 4 段；墨点范围 x 42..79 / y 17..44（第 2、3 段可用 y 16..47）。
#define LOCK_CX        64   // 锁的左右中心
#define LOCK_R          7   // 锁梁半圆的半径
#define LOCK_CY        27   // 锁梁半圆的圆心 y（闭合位置）
#define LOCK_LEG        8   // 锁梁两条腿的长度（含伸进锁体那一段）
#define LOCK_BODY_X    49   // 锁体左边缘 = LOCK_CX - (LOCK_BODY_W-1)/2
#define LOCK_BODY_Y    31   // 锁体顶边
#define LOCK_BODY_W    31   // 锁体宽，取奇数锁体才正好以 LOCK_CX 为中心
#define LOCK_BODY_H    14   // 锁体高
#define LOCK_BODY_R     3   // 锁体圆角半径
#define LOCK_SEG       12   // 锁梁半圆采样成几段折线
#define LOCK_OPEN_DEG  90   // 完全打开时锁梁转过的角度（90 = 躺平指向左）
#define LOCK_ANIM_MS  700   // 动画时长；之后一直停在打开状态
#define LOCK_DEG2RAD   0.017453292f
#define LOCK_PI        3.14159265f


// ============================ 状态 ============================
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
static uint32_t unlockNoticeStartMs = 0;   // "已解锁" 提示是何时开始的（开锁动画要用）
static uint32_t unlockNoticeEndMs   = 0;   // "已解锁" 提示显示到什么时候
static bool     unlockWasManual     = false;  // 本次是主动取消还是超时

// K1/K2 的"已按下一次，正在等第二次"状态，详见 adjustKey()。
// [0] = K1（加），[1] = K2（减）
//
// 这是 static 的，也就是**本应用私有** —— 原来的全局版本会让"在别的应用里
// 按过 K1，切回锁应用时长自己跳了"这种串台发生。
struct ClickState {
  bool     waiting = false;
  uint32_t firstMs = 0;
};
static ClickState click[2];


// ============================ 状态迁移 ============================
static void enterIdle(uint32_t now, bool manual) {
  state               = ST_IDLE;
  remainMs            = 0;
  unlockNoticeStartMs = now;
  unlockNoticeEndMs   = now + UNLOCK_NOTICE_MS;
  unlockWasManual     = manual;
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


// ============================ 脚丫点阵 ============================
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


// ============================ 开锁动画 ============================
// 四舍五入。没用 lroundf 是为了不依赖 math.h 里那个函数是否被声明。
static inline int lockRound(float v) { return (int)(v + (v < 0.0f ? -0.5f : 0.5f)); }

static void drawLock(uint32_t elapsedMs) {
  // 进度 0~100，再过一道缓出曲线：开头转得快、末尾慢下来停住。
  // 匀速转到底会显得很机械，像被人硬拖过去的。
  unsigned p = (unsigned)((uint64_t)elapsedMs * 100 / LOCK_ANIM_MS);
  if (p > 100) p = 100;
  const int e = 100 - (100 - (int)p) * (100 - (int)p) / 100;

  // 锁体：全程不动。这个尺寸（31x14, r=3）不落进 drawRFrame 的退化区，
  // 可以放心用 —— 只有"圆角吃满整个高度"的胶囊才需要手拼，见下一个函数。
  u8g2.drawRFrame(LOCK_BODY_X, LOCK_BODY_Y, LOCK_BODY_W, LOCK_BODY_H, LOCK_BODY_R);
  u8g2.drawDisc(LOCK_CX, LOCK_BODY_Y + (LOCK_BODY_H - 1) / 2, 2, U8G2_DRAW_ALL);

  // 锁梁当成刚体：所有点都在"相对铰链"的局部坐标里定义，统一转完再平移。
  // 屏幕 y 向下，所以角度取负才是视觉上的"向左上方转"。
  const float th = (float)LOCK_OPEN_DEG * (float)e / 100.0f * LOCK_DEG2RAD;
  const float c  = cosf(-th), s = sinf(-th);
  const int   hx = LOCK_CX - LOCK_R;      // 铰链 x：左腿根部
  const int   hy = LOCK_CY + LOCK_LEG;    // 铰链 y

  // 局部坐标 (ox, oy) → 屏幕坐标。ox/oy 都是编译期常量，展开两次没有副作用。
  #define LOCK_PT(ox, oy, px, py)                             \
      const int px = hx + lockRound((ox) * c - (oy) * s);     \
      const int py = hy + lockRound((ox) * s + (oy) * c)

  // 两条腿
  LOCK_PT(0, 0, lx0, ly0);
  LOCK_PT(0, -LOCK_LEG, lx1, ly1);
  LOCK_PT(2 * LOCK_R, 0, rx0, ry0);
  LOCK_PT(2 * LOCK_R, -LOCK_LEG, rx1, ry1);
  u8g2.drawLine(lx0, ly0, lx1, ly1);
  u8g2.drawLine(rx0, ry0, rx1, ry1);

  // 半圆采成折线。半径只有 7，12 段已经看不出棱角了。
  // 首末两点的局部坐标恰好等于两条腿的顶端，所以三截自然接上，不用另接。
  int px0 = 0, py0 = 0;
  for (int i = 0; i <= LOCK_SEG; i++) {
    const float a  = LOCK_PI - LOCK_PI * i / LOCK_SEG;
    const float ox = LOCK_R + LOCK_R * cosf(a);
    const float oy = -LOCK_LEG - LOCK_R * sinf(a);
    const int x = hx + lockRound(ox * c - oy * s);
    const int y = hy + lockRound(ox * s + oy * c);
    if (i > 0) u8g2.drawLine(px0, py0, x, y);
    px0 = x; py0 = y;
  }

  #undef LOCK_PT
}


// ============================ 胶囊进度条 ============================
// 胶囊 = 两端半圆 + 中间两条直边，这里全部手拼，刻意不用 drawRBox/drawRFrame。
//
// 原因是那两个函数把直边长度算成 w-2r / h-2r，并且带 "长度 >= 3 才画" 的
// 判断（见 src/clib/u8g2_box.c 的 u8g2_DrawRBox / u8g2_DrawRFrame）。
// 胶囊要的正是 r = h/2 的完整半圆端头，此时 h-2r = 0，直边整段被跳过，
// 两端反而会开一个缺口。所以这种"圆角吃满整个高度"的形状必须自己拼。
static void drawTrackCapsule() {
  // 两端半圆：四个象限是位标志（u8g2.h 里 0x01~0x08），OR 起来就是半圆
  u8g2.drawCircle(BAR_X + BAR_R, BAR_CY, BAR_R,
                  U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
  u8g2.drawCircle(BAR_X + BAR_W - 1 - BAR_R, BAR_CY, BAR_R,
                  U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
  // 上下两条直边，接在两个半圆之间
  u8g2.drawHLine(BAR_X + BAR_R, BAR_CY - BAR_R, BAR_W - 2 * BAR_R);
  u8g2.drawHLine(BAR_X + BAR_R, BAR_CY + BAR_R, BAR_W - 2 * BAR_R);
}

// 走过的部分：宽度随进度长的实心胶囊。w = 填充能走的像素数。
static void drawFillCapsule(int w) {
  if (w <= 0) return;
  const int x0 = BAR_X + BAR_PAD;

  // 长度不满一个圆角直径时画不成胶囊（两个端头圆会重叠成一坨），
  // 兜底画一个圆点。这样刚按住 K4 的那一刻就有反馈，而不是等到
  // 进度攒够 6% 才出现 —— 按下没反应是最容易让人以为"按钮坏了"的。
  if (w < 2 * BAR_RF + 1) {
    u8g2.drawDisc(x0 + BAR_RF, BAR_CY, BAR_RF, U8G2_DRAW_ALL);
    return;
  }

  u8g2.drawDisc(x0 + BAR_RF, BAR_CY, BAR_RF, U8G2_DRAW_ALL);              // 左端头
  u8g2.drawDisc(x0 + w - 1 - BAR_RF, BAR_CY, BAR_RF, U8G2_DRAW_ALL);      // 右端头
  u8g2.drawBox(x0 + BAR_RF, BAR_CY - BAR_RF,                              // 中间
               w - 2 * BAR_RF, 2 * BAR_RF + 1);
}

static void drawUnlockProgress(uint32_t held) {
  unsigned pct = (unsigned)((uint64_t)held * 100 / CANCEL_HOLD_MS);
  if (pct > 100) pct = 100;

  // 第 2、3 行：脚丫钉死不动，羽毛按进度从 FEATHER_CX0 飘到 FEATHER_CX1。
  // 这里是整数 lerp：羽轴 x = 起点 + (终点 - 起点) * pct / 100，省掉浮点。
  const int featherCx = FEATHER_CX0 + (FEATHER_CX1 - FEATHER_CX0) * (int)pct / 100;
  u8g2.drawXBMP(FOOT_X, FOOT_Y, FOOT_W, FOOT_H, ICON_FOOT);
  drawFeather(featherCx, ANIM_BASE_Y);

  // 第 4 行：整行宽的胶囊进度条。
  // 空轨道一直画，这样 0% 时也看得见总长 —— 光有填充看不出还剩多少。
  drawTrackCapsule();
  drawFillCapsule((BAR_W - 2 * BAR_PAD) * (int)pct / 100);
}


// ============================ 输入 ============================
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
//
// 框架对 K4 用的是同一套思路（见主 .ino 的 k4Shape），两处保持一致。
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


// ============================ 应用接口 ============================
void lockEnter(void) {
  // 目前没有进入时要做的初始化 —— 状态本来就是"上电即空闲"。
}

void lockLeave(void) {
  // 把挂起的单击清掉。不清的话，用户在这个应用里按了 K1 就切走，
  // 300ms 后那次单击到期补发，会改到一个"已经不在前台"的应用的设定值上。
  // 症状就是"去设置转一圈回来，时长自己跳了 1 小时"。
  click[0].waiting = click[1].waiting = false;
}

void lockTick(uint32_t now) {
  timerTick(now);
}

void lockInput(uint32_t now) {
  if (state == ST_IDLE) {
    adjustKey(0, +1, now);                      // K1 增加时长
    adjustKey(1, -1, now);                      // K2 减少时长

    if (btn[2].pressEdge) startTimer(now);      // K3 启动

    // K4 归零重设。这里读的是框架整形过的单击，不是原始的 pressEdge ——
    // 原来的 K4 还兼着"返回桌面"，双击必须能撤销第一下，所以单击得等一个
    // 双击窗口（≤250ms）。这是用户确认过的取舍。
    if (k4TapTake()) {
      setMinutes = 0;
      unlockNoticeEndMs = 0;
      click[0].waiting = click[1].waiting = false;
    }
  } else {                                      // ST_RUNNING
    // 只有 K4 长按满 CANCEL_HOLD_MS 才能中断。
    // 注意这里刻意不用 pressEdge，否则一碰就取消了。
    // 长按不发 tap，所以取消完松手不会补一次"归零"把设定值清成 0。
    if (k4HeldMs(now) >= CANCEL_HOLD_MS) {
      enterIdle(now, UNLOCK_MANUAL);
    }
  }
}

void lockDraw(uint32_t now) {
  // 不 clearBuffer / 不 sendBuffer —— 那是框架的事。
  u8g2.drawHLine(0, BAND_SEP_Y, 128);   // 第 1 段与下方内容之间的一条分隔线

  if (state == ST_RUNNING) {
    const uint32_t held = k4HeldMs(now);

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
    // 刚归零或刚取消，提示几秒。两种情况文案分开，别让主动取消显示成"时间到"。
    // 第 2、3 段让给开锁动画，原来的大号 00:00:00 撤掉 —— 这一屏要传达的是
    // "锁开了"，而不是"时间归零了"。
    //
    // 归零和主动取消**共用这段动画是用户确认过的**，不是漏改：两种情况下
    // 锁确实都开了，配开锁动画不违和。所以别看到 unlockWasManual 就以为
    // 这里该分叉——要分叉的只有文案，动画不分。
    drawBand(0, unlockWasManual ? "已取消" : "已解锁");
    drawLock(now - unlockNoticeStartMs);
    drawBand(3, unlockWasManual ? "手动取消 拨片已打开" : "时间到  拨片已打开");
  } else {
    // 待机，正在设定时长
    drawBand(0, "定时锁  待机中");
    drawBigTime((uint32_t)setMinutes * 60);
    // 第 4 段留空，待定
  }
}

// 给心跳日志用。做这么个访问器而不是让主 .ino 直接读 state，是因为主 .ino
// 在最前面、看不到本文件里的 static 变量 —— 多文件拼接的硬约束。
// 同一句话也说清了为什么应用表里要有 status 这个字段：框架不该认识任何一个
// 具体的应用。
const char *lockStatus(void) {
  return (state == ST_RUNNING) ? "RUNNING" : "IDLE  ";
}
