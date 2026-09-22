/*
 * ui_kit.ino —— 各应用共用的绘制工具
 *
 * 屏幕 128x64，横向四等分，每段 16px：
 *   第 1 段   —— 标题／状态
 *   第 2+3 段 —— 大号内容（跨两行，合计 32px）
 *   第 4 段   —— 提示
 * 这个版面是定时锁定下来的，但"段"这个概念对别的应用同样好用，所以提到这里。
 *
 * 中文必须用 drawUTF8()，不能用 drawStr()。本文件也必须存为 UTF-8 编码
 * （Arduino IDE 默认如此），否则中文会乱码。
 *
 * 这里只放"和具体应用无关"的东西。定时锁自己的画法（脚丫、羽毛、锁、胶囊
 * 进度条）在 app_lock.ino 里，那些搬过来才内聚。
 *
 * ⚠️ 版面的那几个尺寸宏（BAND_H / BAND_SEP_Y / BIG_TIME_BASE 等）**不在这个
 * 文件里**，而是放在主 .ino 的显示配置区。原因是 .ino 的拼接顺序按字母序，
 * 本文件排在最后（u 开头），而 app_lock.ino 要用 BAND_SEP_Y —— 宏是文本替换，
 * 定义点必须在用处之前，放在这里就晚了。
 *
 * 同理，下面两个函数在 app.h 里声明而不是在这里 static：app_lock.ino 在
 * 本文件之前就要调用它们。
 */

static inline int bandBaseline(int i) { return BAND_BASE0 + BAND_H * i; }

// 段位编号 0~3，从上到下
void drawBand(int i, const char *s) {
  u8g2.setFont(CN_FONT);
  u8g2.drawUTF8(2, bandBaseline(i), s);
}

// 把秒数画成 HH:MM:SS，用大号数字字体，按实测宽度水平居中
void drawBigTime(uint32_t totalSecs) {
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
