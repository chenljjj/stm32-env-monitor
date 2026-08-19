# 主机侧检查

在项目根目录运行协议测试：

```powershell
python tools/protocol_selftest.py
```

该脚本验证 30 字节 UART 帧、CRC-16/CCITT-FALSE 参数、字段偏移、带符号温度编码和 MQTT JSON 示例。在 ARM 编译器可通过 CubeIDE 使用之前，它可以作为协议参考检查。

