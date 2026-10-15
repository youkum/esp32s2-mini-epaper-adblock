#ifndef ADBLOCK_DASHBOARD_H
#define ADBLOCK_DASHBOARD_H

#include <Arduino.h>

// ── E-Paper 仪表盘 ───────────────────────────────────────────
// 将拦截统计渲染到 768x552 四色面板上。
//
// 整幅画面从不作为完整图像一次性生成：epd_driver 逐 band 流式
// 驱动面板，并为每个 band 回调到这里，因此下面两个屏幕各自
// 在每个 band 重绘一次。band 契约见 epd_driver.h，把绘制裁剪
// 到当前 band 的 canvas 辅助函数见 display.h。
//
// 颜色语义（设计上固定不变）：
//   black  — 数据 / 标签
//   red    — 今日拦截的主数字、告警
//   yellow — 次要高亮、排行榜点缀
//   white  — 背景

// 完整仪表盘：主计数、拦截最多的域名列表、每小时柱状条，
// 以及含运行时长与最后刷新时间的页脚。会刷新面板。
void dashboardDraw();

// 简单消息屏，用于启动 / 出错 / 配置场景。文本会被复制，
// 因此调用结束后不必继续有效。会刷新面板。
void dashboardMessage(const char* line1, const char* line2, const char* line3);

#endif // ADBLOCK_DASHBOARD_H
