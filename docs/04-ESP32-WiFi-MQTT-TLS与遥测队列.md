# ESP32 Wi-Fi、MQTT/TLS 与遥测队列

Wi-Fi、MQTT、TLS 等技术缩写保持行业惯用写法。

本文说明 ESP32 网关如何把 STM32 的 UART 环境帧转换为 MQTT 消息，以及当前已经验证和仍待验证的边界。

截至当前版本，UART 采集端已迁移为 STM32 FreeRTOS 多任务架构，ESP32 的 Wi-Fi/MQTT 网关行为与协议载荷保持兼容；采集、OLED、ACK 和 MQTT 遥测已再次完成实板联调。

## 1. 数据路径

```text
STM32 ENV_REPORT
      │ UART2 + CRC16
      ▼
ESP32 协议状态机
      ├── 解包成功：返回 ACK result=0
      └── 载荷错误：返回 ACK result=2
      │
      ▼
FreeRTOS 遥测队列（32 条）
      │
      ▼
独立 MQTT 发布任务
      │
      ▼
Wi-Fi STA → TLS 8883 → EMQX Serverless
      │
      └── 状态聚合器 → NET_STATUS → STM32
```

UART ACK、ESP-MQTT 接受消息和 Broker PUBACK 是三个不同层次：

1. ACK 只表示 ESP32 已正确解析 UART 载荷。
2. `esp_mqtt_client_publish()` 返回非负消息 ID，表示消息已交给 ESP-MQTT。
3. `MQTT_EVENT_PUBLISHED` 表示 QoS 1 流程收到了 Broker 的 PUBACK。

## 2. Wi-Fi 状态处理

`app_wifi` 使用 STA 模式和 ESP-IDF 默认事件循环：

- `WIFI_EVENT_STA_START`：发起连接。
- `WIFI_EVENT_STA_DISCONNECTED`：清除本地连接标志并重新连接。
- `IP_EVENT_STA_GOT_IP`：记录 IPv4；首次获得地址时启动 MQTT 客户端。

Wi-Fi 与 MQTT 事件同时交给 `app_net_status` 聚合。聚合器只在状态真正变化时增加断线计数并通知 UART 层，避免重复事件造成重复计数。

网络事件回调只改变状态和触发连接动作，不在回调中处理 UART 数据。

## 3. MQTT 与 TLS 配置

当前配置：

| 项目 | 值 |
| --- | --- |
| MQTT 版本 | 3.1.1 |
| 传输 | MQTT over TLS |
| 端口 | 8883 |
| 服务器认证 | 固件内嵌 EMQX Serverless CA 证书 |
| 客户端认证 | 用户名和密码 |
| Keep Alive | 60 秒 |
| 重连间隔 | 4 秒 |
| 连接超时 | 10 秒 |

真实 Wi-Fi、Broker 密码保存在被 Git 忽略的 `main/app_secrets.h`。仓库只提供 `main/app_secrets.h.example`，用于说明所需宏，不保存凭据。

## 4. 主题与消息

| 主题 | QoS | Retain | 用途 |
| --- | ---: | --- | --- |
| `stm32-env-monitor/telemetry` | 1 | 否 | 环境遥测 JSON。 |
| `stm32-env-monitor/status` | 1 | 是 | 设备在线状态。 |

MQTT 连接成功后发布 retained `{"online":true}`。异常掉线时 Broker 根据 LWT 发布 retained `{"online":false}`。主动正常断开若需要立即标记离线，应在停止客户端前主动发布离线状态；当前程序持续运行，尚未实现主动停机流程。

遥测 JSON 保留 STM32 原始采样序号、运行时间、有效标志和错误计数。当前没有可靠 Unix 时间来源，因此不伪造云端时间戳；后续可在 ESP32 完成 SNTP 同步后增加绝对时间。

## 5. 有界队列与故障策略

`app_mqtt_init()` 在 UART 开始接收前创建：

- 容量为 32 条的 FreeRTOS 队列；
- 一个独立 MQTT 发布任务；
- 一个表示 MQTT 已连接的事件组位。

UART 主循环只负责入队，不等待 Wi-Fi、TLS 或 Broker。发布任务取出一条记录后等待连接位，再调用 ESP-MQTT；本地发布失败最多尝试 3 次，每次间隔 1 秒。队列已满时丢弃最新样本并累计溢出次数，保证内存占用有上限。

STM32 的 ACK 超时重传会再次发送相同 UART 序号的环境帧。ESP32 识别与上一有效帧相同的连续序号后，只返回 ACK、不再写入遥测队列；运行统计中的 `duplicate` 记录这类已去重帧数。

当前策略适合演示和短时网络抖动，但不是持久化存储：ESP32 断电后队列数据会丢失，长时间断网也会溢出。若业务要求不丢数据，应进一步设计 NVS/Flash 环形日志、写放大控制、记录过期策略和恢复发送速率。

## 6. 运行统计

ESP32 每 10 个样本输出一次：

```text
net=WM wd=0 md=0 queue=0 submitted=10 overflow=0 accepted=10 puback=10 puberr=0 abandoned=0
```

| 字段 | 含义 |
| --- | --- |
| `net` | `W` 表示 Wi-Fi 已连接，`M` 表示 MQTT 已连接。 |
| `wd` / `md` | Wi-Fi / MQTT 从已连接变为断开的累计次数。 |
| `queue` | 队列中尚未被发布任务取出的数量。 |
| `submitted` | 成功加入应用队列的累计样本数。 |
| `overflow` | 队列满而丢弃的累计样本数。 |
| `accepted` | ESP-MQTT 接受的累计遥测数。 |
| `puback` | 收到的 QoS 1 PUBACK 事件数；也可能包含 QoS 1 状态消息。 |
| `puberr` | 本地发布尝试失败次数。 |
| `abandoned` | 三次尝试均失败后放弃的样本数。 |
| `duplicate` | 收到重复 `ENV_REPORT`、已确认但未重复入队的累计次数。 |

## 7. 向 STM32 回传网络状态

ESP32 使用 12 字节 `NET_STATUS` 载荷回传：连接标志、变化原因、Wi-Fi 断线次数和 MQTT 断线次数。触发规则如下：

- ESP32 状态模块启动时发送初始快照；
- Wi-Fi 或 MQTT 连接状态发生变化时立即发送；
- 每处理 10 个 `ENV_REPORT` 再发送一次当前快照，帮助 STM32 复位后恢复状态。

ACK 由 UART 接收任务发送，网络状态由事件回调触发。两者共享 UART2，因此发送函数使用 FreeRTOS 互斥锁，防止两个协议帧的字节交叉。STM32 对载荷长度、标志保留位、原因范围和保留字节做语义校验，合法后保存状态并输出 `net/ns/nerr/wd/md/nr` 诊断字段。

## 8. 验证状态与后续测试

### 已完成硬件验证（2026-08-24）

- ESP32 连接 2.4 GHz Wi-Fi 后获取 IPv4，并成功建立 TLS 8883 MQTT 会话；
- MQTTX 已订阅到连续遥测 JSON，字段、定点数换算、有效标志和四类错误计数均正确；
- STM32 的 ACK 计数持续增长，`rxerr=0`、`ackerr=0`、`retry=0`、`timeout=0`、`drop=0`；
- Wi-Fi 断开时 ESP32 记录 Beacon Timeout，STM32 由 `WM` 变为 `--`；
- 热点恢复后 ESP32 重新关联、获取 IPv4，再恢复 MQTT，STM32 状态依次为 `--` → `W-` → `WM`；
- 断网期间应用队列峰值为 14 条，`overflow=0`；恢复后 `queue=0`、已提交样本均被 MQTT 接受，QoS 1 PUBACK 持续增长。

### 仍待验证

1. 断网超过 32 个采样周期时的队列溢出与丢弃策略。
2. Broker 单独不可达、错误凭据或错误 CA 时的分层诊断与退避行为。
3. ESP32 异常断电后，订阅端是否收到 retained 离线遗嘱。
4. 仅复位 STM32 后，最迟 10 个样本内是否通过周期快照恢复网络状态。
5. 临时断开 ESP32 UART2_TX→STM32 PA10，确认 `timeout`、`retry`、`drop` 与去重上云行为。
6. 至少 30 分钟连续运行，记录重连次数、队列峰值、错误计数和堆内存趋势。
