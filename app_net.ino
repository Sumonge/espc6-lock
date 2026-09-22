/*
 * app_net.ino —— 网络应用
 *
 * ⚠️ 现在是一块**占位**：WiFi / BLE 配网要到计划里的 Step 5~7 才做
 * （服务层放 net_lock.ino，本文件只负责画）。所以这一页显示的每一项都是
 * "未接入"，那不是 bug，是实话。
 *
 * 它为什么这么早就存在：桌面是**一行三格**（用户定的版面），三个格子必须
 * 都有应用，否则 K1/K2 的循环选中会跳过空格子，看起来像按键失灵。
 *
 * 这个文件**没有一行处理 K4** —— 不消费那次单击，框架就当作"返回桌面"。
 * 和 app_settings.ino 一样，这是框架消费规则的回报，不是漏写。
 */

// ============================ 配置区 ============================
// 全页只用 drawBand()（内部固定 CN_FONT），不做中英混排。
//
// 中英混排要小心：setFont 是**全局状态**，不是每个绘制调用的参数。切了
// 英文小字体之后忘了切回来，下一个 drawUTF8 就会用那个字体画汉字 ——
// 而 ASCII 字体的字形表里根本没有汉字，结果是整串空白。真要混排，就得
// 每次画之前 setFont，而不是 set 一次管一片。

static uint8_t netPage = 0;     // 只有一页，留着给 Step 8 加页用


// ============================ 应用接口 ============================
void netEnter(void) {
  netPage = 0;
}

void netLeave(void) {
  // 无挂起状态
}

void netTick(uint32_t now) {
  (void)now;   // 网络状态由服务层自己推，不需要在这里推进
}

void netInput(uint32_t now) {
  (void)now;

  // K1/K2/K3 现在都没有可操作的东西：
  //   K1/K2 翻页 —— 只有一页
  //   K3    配网 —— 要等 BLE 配网做出来
  // 留着空实现而不是干脆不写这个函数：应用表的 tick/input/draw 三个槽位
  // 框架是**无条件调用**的，不能有 nullptr（那要在每处调用点判空）。
}

void netDraw(uint32_t now) {
  (void)now;

  u8g2.drawHLine(0, BAND_SEP_Y, 128);   // 第 1 段与下方内容之间的分隔线

  drawBand(0, "网络");
  drawBand(1, "无线  未接入");     // Step 7：WiFi STA + HTTP/WS
  drawBand(2, "配网  未接入");     // Step 5：BLE 配网 GATT 服务
  // 第 3 段留空，和定时锁待机时一样（用户标记「待定」的那一段）。
  // 等 Step 7 做完，这里放 IP 或者信号强度。
}

// 心跳日志第二段
const char *netStatus(void) {
  return "NONE";    // Step 5 之后改成 STA/AP/OFF 之类
}
