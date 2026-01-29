import asyncio
import struct
import time
from bleak import BleakScanner, BleakClient

DEVICE_NAME  = "ESP32-MP3-H-TRY"
CHAR_TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
CHAR_RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
END_SEQ = 0xFFFFFFFF

async def main():
    # 统计
    rx_bytes_payload = 0
    rx_pkts = 0
    seq_min = None
    seq_max = None
    seq_seen = set()

    # 计时（用高精度单调时钟）
    t_write = None       # PC 写 START 的时间点
    t_first = None       # PC 收到第一包数据的时间点
    t_end = None         # PC 收到 END 包的时间点

    done_event = asyncio.Event()

    def on_notify(_, data: bytearray):
        nonlocal rx_bytes_payload, rx_pkts, seq_min, seq_max, seq_seen
        nonlocal t_first, t_end

        if len(data) < 4:
            return

        now = time.perf_counter()
        seq = struct.unpack(">I", data[:4])[0]
        payload_len = len(data) - 4

        # 第一包数据到达
        if t_first is None and seq != END_SEQ:
            t_first = now

        # END 包
        if seq == END_SEQ:
            t_end = now
            done_event.set()
            return

        rx_pkts += 1
        rx_bytes_payload += payload_len

        seq_seen.add(seq)
        seq_min = seq if seq_min is None else min(seq_min, seq)
        seq_max = seq if seq_max is None else max(seq_max, seq)

    print("Scanning...")
    devs = await BleakScanner.discover(timeout=5.0)

    target = None
    for d in devs:
        if (d.name or "").strip() == DEVICE_NAME:
            target = d
            break

    if not target:
        print("Device not found.")
        for d in devs:
            print(f"  name={d.name} address={d.address}")
        return

    print(f"Connecting to {target.name} @ {target.address}")
    async with BleakClient(target.address) as client:
        print("Connected:", client.is_connected)

        print("Subscribing notify...")
        await client.start_notify(CHAR_TX_UUID, on_notify)

        # 给 Windows 一点时间把订阅真正设好（重要）
        await asyncio.sleep(0.15)

        print("Sending START...")
        t_write = time.perf_counter()
        await client.write_gatt_char(CHAR_RX_UUID, b"\x01", response=False)

        # 等 END（或超时）
        try:
            await asyncio.wait_for(done_event.wait(), timeout=20.0)
        except asyncio.TimeoutError:
            print("[WARN] Timeout waiting for END.")

        # 给尾部回调一点时间（可选）
        await asyncio.sleep(0.05)

        try:
            await client.stop_notify(CHAR_TX_UUID)
        except OSError as e:
            print(f"[WARN] stop_notify failed (ignored): {e}")

    if t_write is None:
        print("START not sent?")
        return
    if t_end is None:
        # 没收到 END：用“最后一次处理时刻”不太靠谱，所以直接提示
        print("No END received (cannot measure full receive time).")
        print(f"Received packets so far: {rx_pkts}, payload bytes: {rx_bytes_payload}")
        return

    dur_write_to_end = t_end - t_write
    dur_first_to_end = (t_end - t_first) if t_first is not None else None
    goodput_Bps = (rx_bytes_payload / dur_first_to_end) if (dur_first_to_end and dur_first_to_end > 0) else 0.0

    print("\n=== RESULTS ===")
    print(f"Duration (write->END): {dur_write_to_end:.3f} s")
    if dur_first_to_end is not None:
        print(f"Duration (1st data->END): {dur_first_to_end:.3f} s")
    else:
        print("Duration (1st data->END): N/A (no data packets)")

    print(f"Received payload bytes: {rx_bytes_payload}")
    print(f"Received packets: {rx_pkts}")

    if seq_min is None or seq_max is None:
        print("No seq packets received.")
        return

    expected = (seq_max - seq_min + 1)
    received_unique = len(seq_seen)
    lost = expected - received_unique
    loss_rate = (lost / expected) if expected > 0 else 0.0

    print(f"Seq range: {seq_min} .. {seq_max}")
    print(f"Expected packets (in range): {expected}")
    print(f"Unique received: {received_unique}")
    print(f"Lost packets: {lost}")
    print(f"Loss rate: {loss_rate*100:.2f}%")
    print(f"Goodput: {goodput_Bps:.1f} B/s  ({goodput_Bps*8/1000:.2f} kbps)")

if __name__ == "__main__":
    asyncio.run(main())
