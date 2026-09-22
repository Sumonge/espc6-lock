/*
 * app_settings.ino —— 设置应用
 *
 * 规划中的三页：蓝牙 / 网络 / 关于。现在**只有「关于」页** —— 另外两页要
 * 展示的是 BLE 配网和 WiFi 的真实状态，那些还没做，先做只能填假数据。
 *
 * 加进来得早，是因为桌面网格需要第二个应用，否则 K1/K2 的循环选中根本
 * 没法测。翻页的骨架已经搭好（SET_PAGE_COUNT + setPage），加页就是改
 * 这个宏 + 在 setDraw 里加一个分支。
 *
 * 整个文件**没有一行处理 K4** —— 不消费那次单击，框架就当作"返回桌面"。
 * 这就是框架那套消费规则的回报：新应用忘了写返回也不会把用户困住。
 */

// ============================ 配置区 ============================
#define SET_PAGE_COUNT  1       // 页数。蓝牙页/网络页做出来后改成 3
#define PAGE_ABOUT      0       // 页码：0 = 关于

static uint8_t setPage = PAGE_ABOUT;


// ============================ 应用接口 ============================
void setEnter(void) {
  setPage = PAGE_ABOUT;   // 每次进来都回到第一页，理由同 deskEnter
}

void setLeave(void) {
  // 无挂起状态
}

void setTick(uint32_t now) {
  (void)now;   // 设置页没有时间推进的东西
}

void setInput(uint32_t now) {
  (void)now;

  // K1/K2 翻页。SET_PAGE_COUNT 现在是 1，所以这两行是空转 —— 这是对的，
  // 不是漏写：`(0 + 1 - 1) % 1 == 0`。页数改成 3 之后它们自动生效。
  if (btn[0].pressEdge) setPage = (uint8_t)((setPage + SET_PAGE_COUNT - 1) % SET_PAGE_COUNT);
  if (btn[1].pressEdge) setPage = (uint8_t)((setPage + 1) % SET_PAGE_COUNT);

  // 第一页（关于）没有可调项，K3 也先不接。
}

void setDraw(uint32_t now) {
  u8g2.drawHLine(0, BAND_SEP_Y, 128);   // 第 1 段与下方内容之间的分隔线

  if (setPage == PAGE_ABOUT) {
    char buf[3][40];

    // 芯片型号 + 固件占用的 Flash。后者是"我烧进去的到底是哪一版"的硬证据，
    // 比版本号可靠 —— 版本号忘了改的情况太多了。
    snprintf(buf[0], sizeof(buf[0]), "ESP32-C6  %uKB",
             (unsigned)(ESP.getSketchSize() / 1024));

    // __DATE__ 是编译时刻，不是烧录时刻；两者不同就说明烧的是旧固件。
    snprintf(buf[1], sizeof(buf[1]), "编译 %s", __DATE__);

    // 运行时长。millis() 约 49 天回绕，对一块要拔电重启才能解锁的定时器
    // 来说碰不到。
    const uint32_t s = now / 1000;
    snprintf(buf[2], sizeof(buf[2]), "运行 %02u:%02u:%02u",
             (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));

    // 标题把版本号带上 —— 关于页最常被问的就是"这是哪一版"。
    char title[24];
    snprintf(title, sizeof(title), "关于本机 v%s", FW_VERSION);

    drawBand(0, title);
    drawBand(1, buf[0]);
    drawBand(2, buf[1]);
    drawBand(3, buf[2]);
  }
}

// 心跳日志第二段：报当前在第几页（页数多了以后，不看屏幕也能知道翻到哪了）
const char *setStatus(void) {
  return (setPage == PAGE_ABOUT) ? "ABOUT" : "?????";
}
