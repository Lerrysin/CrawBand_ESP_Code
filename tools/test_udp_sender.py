"""
CrawlBand — UDP 测试发送器
模拟 Unity 向 ESP32 发送触觉数据包，用于验证网络通信。

用法:
    python test_udp_sender.py [ESP32_IP]

默认目标: 192.168.1.100:9000
"""

import socket
import json
import time
import math
import sys

TARGET_IP   = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.100"
TARGET_PORT = 9000
SEND_HZ     = 30

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

seq = 0
t0 = time.time()

print(f"Sending to {TARGET_IP}:{TARGET_PORT} at {SEND_HZ} Hz")
print("Press Ctrl+C to stop\n")

try:
    while True:
        seq += 1
        t = time.time() - t0

        # 模拟蛇绕手腕滑行
        pos = (t * 0.1) % 1.0  # 10秒转一圈

        packet = {
            "seq": seq,
            "timestamp": t,
            "animal": "snake",
            "wrist": "left",
            "mode": "continuous",
            "pos": round(pos, 4),
            "speed": 0.12,
            "intensity": round(0.5 + 0.1 * math.sin(t * 2), 3),
            "temp": 0.10,
            "accent": round(0.05 + 0.03 * math.sin(t * 3), 3),
            "contactWidth": 0.35,
        }

        data = json.dumps(packet, separators=(",", ":")).encode("utf-8")
        sock.sendto(data, (TARGET_IP, TARGET_PORT))

        if seq % SEND_HZ == 0:
            print(f"  sent {seq} packets, pos={pos:.3f}")

        time.sleep(1.0 / SEND_HZ)

except KeyboardInterrupt:
    print(f"\nStopped. Total packets sent: {seq}")
