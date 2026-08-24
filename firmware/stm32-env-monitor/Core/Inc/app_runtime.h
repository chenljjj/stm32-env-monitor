#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

/* 在启动调度器前完成应用状态与 USART1 ACK 接收初始化。 */
void app_runtime_init(void);

/* 由 CubeMX 生成的 defaultTask 调用，创建应用任务后退出 defaultTask。 */
void app_runtime_start(void);

#endif /* APP_RUNTIME_H */
