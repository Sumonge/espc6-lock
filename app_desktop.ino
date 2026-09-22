/*
 * app_desktop.ino —— 桌面：3x2 图标网格
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
 *   - 导航是**线性循环**，不是二维方向键。两个键就能到任何地方，加应用时
 *     这段逻辑一个字都不用改。
 *   - 图块标签是**固定的应用名**，不显示剩余时间之类。网格正好铺满 64px，
 *     没有多余的状态栏。
 */

// ============================ 布局配置区 ============================
// 屏幕 128x64，三列两行铺满：
//   x = 0, 43, 86   每格 42 宽，列间留 1px 缝
//   y = 0, 32       每格 32 高，行间没有缝
//
// 一个格子竖着只有 32px，要装 16px 图标 + 12px 汉字标签 + 选中框线，
// 余量只有 4 行。下面这几行是**量出来的**，不是估的，别随手调：
//
//     y+0        选中框上边线
//     y+1..y+16  图标
//     y+17       空行   <- 图标和标签之间唯一的呼吸缝
//     y+18..y+29 标签（基线 y+29）
//     y+30,y+31  空行 + 选中框下边线
//
// 改动之后必须跑 `python tools/desktop_sim.py`（几何自检）再看效果，
// 助手看不到屏幕，这是唯一的证据。
#define DESK_COLS        3      // 列数
#define DESK_CELL_W     42      // 格子宽
#define DESK_CELL_H     32      // 格子高
#define DESK_CELL_DX    43      // 列距 = CELL_W + 1
#define DESK_CELL_DY    32      // 行距 = CELL_H

#define DESK_ICON_X  ((DESK_CELL_W - ICON_W) / 2)   // 图标水平居中 → 13
#define DESK_ICON_Y      1      // 图标顶边
#define DESK_LABEL_BASE 29      // 标签基线（相对格子顶边）
#define DESK_FRAME_R     3      // 选中框的圆角半径

// 选中样式：整格圆角框（用户在三种方案里选的这个）。
// 42x32 配 r=3 不落进 drawRFrame 的退化区（直边 36 / 26，都 >= 3），
// 可以放心用 —— 只有"圆角吃满整个高度"的胶囊才需要手拼，见 CLAUDE.md 坑 6。
//
// 另外两种落选方案（图标聚焦环 18x18、标签反白）的代码在
// tools/desktop_sim.py 里留着，想换回去照着重画即可，几何都验过。

static uint8_t deskSel = 0;     // 选中的格子下标，同时也是 APPS[] 的下标


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

  // 只用 pressEdge，**刻意不接 repeatEdge**：现在网格里只有两个应用，
  // 连发会变成每秒 8 次来回抖，看起来像坏了。等网格铺满 6 格再把它加进来
  // （改法就是这两个判断各加一个 `|| b.repeatEdge`）。
  if (btn[0].pressEdge) deskSel = (uint8_t)((deskSel + APP_COUNT - 1) % APP_COUNT);
  if (btn[1].pressEdge) deskSel = (uint8_t)((deskSel + 1) % APP_COUNT);
  if (btn[2].pressEdge) appOpen(deskSel);

  // K4 一个字都不用写：不消费那次单击，框架就当作"返回桌面"，
  // 而桌面已经在桌面上了，appBackToDesktop() 是幂等的。
}

void deskDraw(uint32_t now) {
  (void)now;

  for (uint8_t i = 0; i < APP_COUNT; i++) {
    const int x = (i % DESK_COLS) * DESK_CELL_DX;
    const int y = (i / DESK_COLS) * DESK_CELL_DY;

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
    u8g2.setFont(CN_FONT);
    const int w = (int)u8g2.getUTF8Width(APPS[i]->name);
    u8g2.drawUTF8(x + (DESK_CELL_W - w) / 2, y + DESK_LABEL_BASE, APPS[i]->name);
  }
}

// 心跳日志第二段。桌面没有"状态"，就报当前选中谁，调试时正好用得上。
const char *deskStatus(void) {
  return APPS[deskSel]->name;
}
