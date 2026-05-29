# examples/lt Performance Modeling Lab

## What This Lab Is

`examples/lt` 现在是 Renode/SystemC LT 示例之上的第一阶段 SystemC/TLM SoC performance modeling lab。

当前阶段的目标很窄：在不改变原有 TLM routing、target 行为和 Renode 接入方式的前提下，记录每次 blocking `b_transport` transaction 的 target-side annotated delay，并用 CSV 与 Python 报告工具观察 initiator、target、command 和 response status 维度的延迟分布。

这不是完整 NoC 或互连性能模型；它是一个可运行、可回归、可继续演进的测量基线。

## Architecture

入口与顶层：

- `sc_main`: `examples/lt/systemc/src/main.cpp`
  - 读取 Renode bridge 的 address/port 参数。
  - 构造 `top top("top", renode_address, renode_port)`。
  - 调用 `sc_core::sc_start()`。
- `top` module:
  - 声明在 `examples/lt/systemc/include/top.h`。
  - 连线在 `examples/lt/systemc/src/top.cpp`。

核心结构：

- Bus: `SimpleBusLT<3, 2> m_bus`
  - 文件：`examples/lt/systemc/third-party/systemc-lt-example/SimpleBusLT.h`
  - 3 个 initiator-facing target sockets：两个 SystemC traffic generator，加一个 Renode bridge。
  - 2 个 target-facing initiator sockets：target 201 和 target 202。
- Initiator 101/102:
  - 创建位置：`examples/lt/systemc/src/top.cpp`
  - `m_initiator_1("m_initiator_1", 101, 0x0, 0x10000000)`
  - `m_initiator_2("m_initiator_2", 102, 0x0, 0x10000000)`
  - 类型：`initiator_top`，内部包含 `traffic_generator` 和 `lt_initiator`。
- Target 201:
  - 创建位置：`examples/lt/systemc/src/top.cpp`
  - 实例：`m_at_and_lt_target_1`
  - 类型：`at_target_1_phase`
  - delay 配置：accept `20 ns`，read response `100 ns`，write response `60 ns`。
- Target 202:
  - 创建位置：`examples/lt/systemc/src/top.cpp`
  - 实例：`m_lt_target_2`
  - 类型：`lt_target`
  - delay 配置：accept `10 ns`，read response `50 ns`，write response `30 ns`。
- Renode bridge:
  - 成员：`renode_bridge m_renode_bridge`
  - 声明位置：`examples/lt/systemc/include/top.h`
  - 构造位置：`examples/lt/systemc/src/top.cpp`
  - 接入位置：`m_renode_bridge.initiator_socket(m_bus.target_socket[2])`
  - CSV 中记录为 `initiator_id = 9002`。

Blocking transaction 路径：

```text
traffic_generator
  -> lt_initiator::initiator_thread()
  -> initiator_socket->b_transport(...)
  -> SimpleBusLT::initiatorBTransport(...)
  -> target b_transport/custom_b_transport
  -> memory::operation(...)
```

Renode 侧 `sysbus` read/write 通过 `renode_bridge` 作为第三个 initiator 接入同一个 `SimpleBusLT`。

## Trace Fields

CSV 输出文件：

```text
examples/lt/results/latency_trace.csv
```

该文件由 `lt` 可执行文件运行时生成；fresh checkout 或尚未运行 `lt.robot` 时，`examples/lt/results/latency_trace.csv` 可能不存在。

字段：

- `initiator_id`: 发起方 ID。当前 `101`、`102` 来自 SystemC traffic generator，`9002` 表示 Renode bridge。
- `target_id`: 目标 ID。当前 `201` 对应 `at_target_1_phase`，`202` 对应 `lt_target`。
- `command`: TLM command，当前输出 `READ`、`WRITE` 或 `OTHER`。
- `address`: bus 改写前的原始 transaction address。
- `data`: payload 前 4 bytes 按 `uint32_t` 解析；不可用时为 `0`。
- `start_time_ns`: transaction 到达 `SimpleBusLT::initiatorBTransport()` 时的 SystemC timestamp。
- `delay_ns`: target `b_transport` 调用对 `sc_core::sc_time& t` 增加的本次 annotated delay。
- `end_time_ns`: `start_time_ns + delay_ns`。
- `decoded_port`: `SimpleBusLT` 从原始 address 解码出的 target port。
- `masked_address`: `trans.set_address(...)` 后传给 target 的地址。
- `data_length`: `trans.get_data_length()`。
- `response_status`: target `b_transport` 返回后的 `trans.get_response_string()`。

## How To Build And Run

Ubuntu 环境示例：

```bash
cd /home/leo/renode-systemc-examples/examples/lt

source /home/leo/tools/renode_1.16.1-dotnet_portable/renode-env

cmake -S . -B build -DCMAKE_PREFIX_PATH=/home/leo/local/systemc
make -C build -j"$(nproc)"

# 当前 Renode script 使用 examples/lt/bin/lt；远程环境中该路径应指向 build/lt。
mkdir -p bin
ln -sf ../build/lt bin/lt

cd /home/leo/renode-systemc-examples
renode-test examples/lt/lt.robot

python3 examples/lt/tools/analyze_latency.py \
  --trace examples/lt/results/latency_trace.csv
```

`lt.robot` 当前包含两个 Renode 测试用例，分别通过 `sysbus WriteDoubleWord` / `ReadDoubleWord` 访问 target 201 与 target 202 对应地址区间。

## How To Analyze

分析脚本：

```text
examples/lt/tools/analyze_latency.py
```

支持参数：

- `--trace <csv_path>`: 指定 CSV。默认读取 `examples/lt/results/latency_trace.csv`。
- `--initiator <id>`: 只保留指定 initiator，可重复。
- `--exclude-initiator <id>`: 排除指定 initiator，可重复。
- `--target <id>`: 只保留指定 target，可重复。
- `--command <READ|WRITE|OTHER>`: 只保留指定 command，可重复。
- `--min-start-time-ns <value>`: 只保留 `start_time_ns >= value` 的 transaction。
- `--max-start-time-ns <value>`: 只保留 `start_time_ns <= value` 的 transaction。
- `--dedup-identical`: 按 transaction 字段去掉完全重复的行；用于避免 `lt.robot` 多个测试用例重复启动 SystemC traffic generator 后重复统计 101/102 的相同 transaction。

常用命令：

```bash
# 完整 trace
python3 examples/lt/tools/analyze_latency.py

# 只看 SystemC 内部 traffic generator
python3 examples/lt/tools/analyze_latency.py \
  --initiator 101 \
  --initiator 102 \
  --max-start-time-ns 10000 \
  --dedup-identical

# 只看 Renode bridge traffic
python3 examples/lt/tools/analyze_latency.py \
  --initiator 9002

# 排除 Renode bridge
python3 examples/lt/tools/analyze_latency.py \
  --exclude-initiator 9002
```

报告包含 overview、按 initiator/target/command 的 delay summary、response status 统计、decoded port 统计、address range summary、data length summary、sanity checks，以及 first/last timeline rows。

## Current Modeling Meaning

当前 `delay_ns` 是 target-side annotated delay。它来自 target accept delay 与 memory read/write response delay 的组合，不包含 bus arbitration、contention、queueing 或 bandwidth saturation。

当前配置下的 nominal delay：

| target_id | command | delay |
| --- | --- | --- |
| 201 | READ | 120 ns |
| 201 | WRITE | 80 ns |
| 202 | READ | 60 ns |
| 202 | WRITE | 40 ns |

计算来源：

- target 201: accept `20 ns` + read `100 ns` / write `60 ns`
- target 202: accept `10 ns` + read `50 ns` / write `30 ns`

Initiator 101 和 102 当前是对称配置：它们使用相同的 base address 参数，通过同一个 `SimpleBusLT` 访问相同两个 target。当前模型不会因为 101/102 同时访问同一 target 而自动产生排队等待。

## Current Limitations

当前还没有实现：

- arbitration model
- contention model
- queueing latency
- bandwidth saturation
- outstanding transaction modeling
- AT/non-blocking timing path analysis

因此，当前报告适合验证 transaction 路径、target delay 配置、Renode bridge 接入和 workload 分离；不适合直接解释真实 SoC interconnect 的拥塞行为。

## Suggested Next Phase

Phase 3 建议在 `SimpleBusLT` 中加入最小 bus arbitration / queueing model。

目标是观察两个 initiator 访问同一 target 时的排队等待、service time、head-of-line blocking 和 tail latency。第一版可以保持 blocking LT 接口不变，只在 `SimpleBusLT::initiatorBTransport()` 内维护每个 target port 的 next-available time，并把 queueing delay 作为额外 annotated delay 写入 trace。
