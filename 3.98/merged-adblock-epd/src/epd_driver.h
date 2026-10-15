#ifndef ADBLOCK_EPD_DRIVER_H
#define ADBLOCK_EPD_DRIVER_H

#include <Arduino.h>

// ── JDY79668 7.5 英寸四色电子纸驱动 ─────────────────────────
// 移植自 InkSight 的 epd_driver.cpp（bit-bang 软件 SPI、面板专用
// 初始化序列），仅保留 EPD_PANEL_75_JDY79668 这一路径。
//
// 面板为 768x552、每像素 2 位，整幅画布共 105,984 字节，超出
// ESP32-S2 的 DRAM 容量，因此图像按分带（band）渲染并流式发送。

void gpioInit();
void epdInit();
void epdSleep();

// ── 帧传输：拆成两段，让面板在后台完成刷新 ──────────────────
// 组帧并推送全部像素约需 1.4 s；随后面板在搬运颜料期间还会把 BUSY
// 继续拉低约 15 s。若在绘制调用里干等这段时间，仪表盘每重绘一次，
// DNS 引擎就会停摆 15 s、无法应答查询。
//
// 因此 epdSendFrame() 只做阻塞的那一半——复位、上电、各分带、
// 全窗口恢复以及 0x12 刷新命令——并在玻璃开始刷新后立即返回。
// 主循环随后反复调用 epdServiceRefresh() 直至其返回 true；正是这个
// 函数在 BUSY 变高后给面板断电（0x02）。两个调用都不等待玻璃，
// 因此整个刷新期间网络始终正常运转。

// 渲染整块面板，每次发送一个最多 EPD_CHUNK_ROWS 行的分带。
// renderBand() 每个分带被调用一次，必须把从旋转前行 rBase 开始的
// rows 个旋转前行，按绘制辅助函数所用的 2bpp 布局填入 buf
// （每字节 4 像素、最高位的像素对在最左、每行 W/4 字节）。缓冲区
// 由驱动持有，长度为 BAND_BUF_LEN 字节。
//
// 返回流式发送耗时（毫秒）；若本次调用先等待了上一次刷新完成，
// 则返回 0。上一帧若仍在刷新，会先等服务它结束再开始新帧。
uint32_t epdSendFrame(void (*renderBand)(int rBase, int rows, uint8_t* buf));

// 已发送的帧仍在刷新面板期间返回 true。
bool epdRefreshActive();

// 由主循环调用。无待处理事项时返回 true；在 BUSY 变高后的首次调用
// 中发送断电命令并记录帧耗时。玻璃仍在刷新期间返回 false。
bool epdServiceRefresh();

// 阻塞直到进行中的刷新被处理完毕。仅在无其他代码能代为轮询时才需要。
void epdWaitRefresh();

#endif // ADBLOCK_EPD_DRIVER_H
