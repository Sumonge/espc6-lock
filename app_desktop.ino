/*
 * app_desktop.ino —— 桌面：状态栏 + 一行三格图标
 *
 * 桌面**是一个普通应用**，不是"curApp == nullptr"那种特殊状态。理由是
 * 每处 curApp->draw() 都要为那个 nullptr 判一次空，漏一处就是空指针崩溃；
 * 而桌面和应用对框架的接口完全一致，差别只在画什么。
 *
 * 桌面不进 APPS[] —— 它不该出现在自己的网格里。它是应用表的**视图**，
 * 而不是表里的一项。
 *
 * 按键：
 *   K1/K2  选中的格子循环（到头顶绕回）
 *   K3     进入选中的应用
 *   K4     单击/双击都由框架处理成"返回桌面"，这里什么都不用写
 *
 * 决策记录（用户拍板，别推翻）：
 *   - 版面 = **状态栏 16px + 图标行 32px，第 4 段留空**。当时给了三种
 *     （A 两行 48px 大图标 / B 现在这种 / C 换成操作提示行），用户选了 B。
 *     16/32/16 正好上下对称，图标行是居中的。
 *   - 导航是**线性循环**，不是二维方向键。两个键就能到任何地方，加应用时
 *     这段逻辑一个字都不用改。
 *   - 图块标签是**固定的应用名**，不显示剩余时间之类。
 *
 * ⚠️ 一行三格意味着桌面**最多三个应用**。第 4 个得先改版面，见 app.h 里
 * APP_COUNT 上面那段。改任何几何之前跑 `python tools/desktop_sim.py`。
 */

// ============================ 布局配置区 ============================
//   y0..y15    状态栏
//   y16..y47   图标行，三格各 42x32（= 128 三等分，列间 1px 缝）
//   y48..y63   留空
//
// 格子内部还是 32px 那套，和两行版完全一样，只是整体下移了 16px。
// 每一行都是**量出来的**，不是估的，别随手调：
//
//     y+0        选中框上边线
//     y+1..y+16  图标
//     y+17,y+18  空行  <- 图标与标签之间的呼吸缝，实测 2 行
//     y+19..y+29 标签墨迹（高 11，基线 y+29）
//     y+30,y+31  空行 + 选中框下边线
//
// 缝是 2 行而不是 1 行 —— 早先按"汉字墨迹 12px 高"算才得出 1 行，把字真
// 画出来量过之后是 11px（证据在 tools/fontcheck/bbox.c）。这条余量很薄：
// 改标签字号或把基线往上挪都会先吃掉它。
#define DESK_COLS        3      // 列数（= 应用数上限，见 app.h）
#define DESK_CELL_W     42      // 格子宽
#define DESK_CELL_H     32      // 格子高
#define DESK_CELL_DX    43      // 列距 = CELL_W + 1
#define DESK_ROW_Y      16      // 图标行顶边 = 状态栏高度，两处必须是同一个数

#define DESK_ICON_X  ((DESK_CELL_W - ICON_W) / 2)   // 图标水平居中 → 13
#define DESK_ICON_Y      1      // 图标顶边
#define DESK_LABEL_BASE 29      // 标签基线（相对格子顶边）
#define DESK_FRAME_R     3      // 选中框的圆角半径

// 选中样式：整格圆角框（用户在三种方案里选的这个）。
// 42x32 配 r=3 不落进 drawRFrame 的退化区（直边 36 / 26，都 >= 3），
// 可以放心用 —— 只有"圆角吃满整个高度"的胶囊才需要手拼，见 CLAUDE.md 坑 6。

// ------------------------------------------------------------------
//  状态栏（y0..y15）
//
//  三项：锁状态（左，**今天就是真值**）+ 信号（中右）+ 电量（右）。
//
//  状态栏**只画在桌面上**，不做成框架级的全局元素。手机的状态栏到处都在，
//  但这里不行：定时锁应用的四段版面（第 1 段标题 + 第 2/3 段大号时间 +
//  第 4 段胶囊进度条）是用户指定要保留的，全局状态栏会把那四段整体压掉
//  16px，大号数字就没地方放了。要做全局的，得先重新排一遍每个应用的版面。
//
//  ⚠️ 后两项今天都读不到，画成**空心**。这不是"信号为零"也不是"电池没电"，
//  是"还没数"。画成实心会让人以为电量真是 0 —— 那是主动误导，比不画更坏。
//
//  信号要等计划里的 Step 7（net_lock.ino 接上 WiFi）。电量**更远**：PCB 的
//  引脚已经用满，没有 ADC 脚可接分压（hardware/NETLIST.md §4.3），真要做
//  只能挂一颗 I2C 电量计（MAX17048，地址 0x36，不和屏幕的 0x3C 冲突）。
//
//  基线 y=12：汉字墨迹相对基线 -10..0，正好落在 y2..y12，上留 2 行下留 3 行。
//  横向位置全部按实测宽度排的（tools/fontcheck）：「已锁」24px 占 x2..x25，
//  信号组 x84..x102，电池 x111..x125，右边缘留 2px。
// ------------------------------------------------------------------
#define BAR_BASE        12      // 状态栏文字的基线
#define BAR_LOCK_X       2      // 锁状态文字左端

#define SIG_X           84      // 信号组左端
#define SIG_BAR_W        4      // 每根柱子宽
#define SIG_BAR_DX       5      // 步进 = 宽 + 1px 缝
#define SIG_BASE_Y      13      // 柱子底边（比文字基线低 1 行，视觉上才和汉字齐）
#define SIG_BAR_MIN_H    4      // 最矮那根的高度
#define SIG_BAR_STEP     3      // 每根递增的高度
// 四根的高度依次是 4/7/10/13（= MIN_H + i*STEP），最高的顶到 y1。
// tools/desktop_sim.py 里是同一个式子，改这里要同步改那边。

#define BATT_X         111      // 电池壳左端
#define BATT_W          13
#define BATT_H           9
#define BATT_Y           4      // 上下各留 4 行，视觉居中
#define BATT_NUB_W       2      // 正极凸点
#define BATT_NUB_H       3

static uint8_t deskSel = 0;     // 选中的格子下标，同时也是 APPS[] 的下标


// ============================ 状态栏数据源 ============================
// 做成函数而不是在 deskDrawStatusBar() 里直接写字面量：将来接上真实数据，
// 只要改这两处，绘制代码一行都不用动。
//
// 返回 <0 一律表示"读不到" → 画空心。**不要**用 0 表示读不到：0 是个合法的
// 电量（真的没电了）和合法的信号格数，两者必须能区分开。
static int deskSignalBars(void) { return -1; }   // 0..4 格；Step 7 接 WiFi
static int deskBatteryPct(void) { return -1; }   // 0..100%；要加电量计硬件


// ============================ 状态栏绘制 ============================
static void deskDrawStatusBar(void) {
  u8g2.setFont(CN_FONT);

  // 左：锁状态。真值，来自锁应用 —— 桌面文件排在 app_lock.ino 之前，
  // 看不见那里的 state，只能走 app.h 里声明的 lockIsRunning()。
  u8g2.drawUTF8(BAR_LOCK_X, BAR_BASE, lockIsRunning() ? "已锁" : "开锁");

  // 中右：信号。四根从矮到高、底边对齐的柱子。
  // 用 drawFrame（直角）**不是 drawRFrame** —— 最矮那格只有 4x4，
  // drawRFrame 在 w-2r < 3 时会跳过直边，画出来是四个断角（CLAUDE.md 坑 6）。
  const int sig = deskSignalBars();
  for (int i = 0; i < 4; i++) {
    const int hgt = SIG_BAR_MIN_H + i * SIG_BAR_STEP;
    const int bx  = SIG_X + i * SIG_BAR_DX;
    const int by  = SIG_BASE_Y - hgt + 1;
    if (sig > i) u8g2.drawBox(bx, by, SIG_BAR_W, hgt);
    else         u8g2.drawFrame(bx, by, SIG_BAR_W, hgt);
  }

  // 右：电池。外壳恒画（那是"这里有个电池"），只有拿到读数才按比例填内胆。
  u8g2.drawFrame(BATT_X, BATT_Y, BATT_W, BATT_H);
  u8g2.drawBox(BATT_X + BATT_W, BATT_Y + (BATT_H - BATT_NUB_H) / 2,
               BATT_NUB_W, BATT_NUB_H);

  const int pct = deskBatteryPct();
  if (pct >= 0) {
    int inner = (BATT_W - 4) * (pct > 100 ? 100 : pct) / 100;
    if (inner < 1) inner = 1;     // 0% 也留一格，否则和"读不到"长得一样
    u8g2.drawBox(BATT_X + 2, BATT_Y + 2, inner, BATT_H - 4);
  }
}


// ============================ 应用接口 ============================
void deskEnter(void) {
  // 每次回桌面都从第一格开始，**刻意不记住上次的位置**：
  // 从设置里按 K4 返回，如果光标还停在"设置"上，再按一次 K3 又进去了，
  // 用户会以为"返回"没生效。
  deskSel = 0;
}

void deskLeave(void) {
  // 没有挂起状态要清。选中项在 deskEnter 里重置。
}

void deskTick(uint32_t now) {
  (void)now;   // 桌面没有时间推进的东西；倒计时归零由锁应用自己的 tick 管
}

void deskInput(uint32_t now) {
  (void)now;

  // 只用 pressEdge，**刻意不接 repeatEdge**：三个应用绕一圈只要按两下，
  // 连发会让选中框每秒跳八格，看起来像坏了。
  // （这一行原来写的是"等网格铺满 6 格再加连发"—— 那个前提随 1x3 版面
  //   作废了：现在**永远只有 3 格**，所以连发不会有加进来的那一天。）
  if (btn[0].pressEdge) deskSel = (uint8_t)((deskSel + APP_COUNT - 1) % APP_COUNT);
  if (btn[1].pressEdge) deskSel = (uint8_t)((deskSel + 1) % APP_COUNT);
  if (btn[2].pressEdge) appOpen(deskSel);

  // K4 一个字都不用写：不消费那次单击，框架就当作"返回桌面"，
  // 而桌面已经在桌面上了，appBackToDesktop() 是幂等的。
}

void deskDraw(uint32_t now) {
  (void)now;

  deskDrawStatusBar();

  for (uint8_t i = 0; i < APP_COUNT; i++) {
    const int x = (i % DESK_COLS) * DESK_CELL_DX;
    const int y = DESK_ROW_Y;      // 只有一行；APP_COUNT <= 3 由 app.h 保证

    if (i == deskSel) {
      u8g2.drawRFrame(x, y, DESK_CELL_W, DESK_CELL_H, DESK_FRAME_R);
    }

    u8g2.drawXBMP(x + DESK_ICON_X, y + DESK_ICON_Y,
                  ICON_W, ICON_H, APPS[i]->icon);

    // 中文必须 drawUTF8，而且**必须配 getUTF8Width**。用 getStrWidth 量中文
    // 会得到一个垃圾值（"定时锁" 真宽 35，getStrWidth 给 11），标签就会
    // 偏出格子：42 格宽按 11 居中 → 起点 x+15，而实际墨迹 35px → 压到右邻格。
    // 原因见 CLAUDE.md 坑 7 —— 一句话：getStrWidth 内部把 next_cb 设成
    // u8x8_ascii_next，它把每个字节**当成一个独立的字形编码**。
    //
    // 这里必须先 setFont 再量宽：字体是全局状态，量宽用的字体和画字用的
    // 字体不一致时，量出来的宽度是另一个字体的。
    u8g2.setFont(CN_FONT);
    const int w = (int)u8g2.getUTF8Width(APPS[i]->name);
    u8g2.drawUTF8(x + (DESK_CELL_W - w) / 2, y + DESK_LABEL_BASE, APPS[i]->name);
  }
}

// 心跳日志第二段。桌面没有"状态"，就报当前选中谁，调试时正好用得上。
const char *deskStatus(void) {
  return APPS[deskSel]->name;
}
