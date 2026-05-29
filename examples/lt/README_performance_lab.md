# examples/lt Performance Modeling Lab

## What This Lab Is

`examples/lt` 现在是 Renode/SystemC LT 示例之上的 SystemC/TLM SoC performance modeling lab。

当前阶段已经加入最小 target `busy_until` queueing model：在不改变原有地址 decode、target memory 行为和 Renode 接入方式的前提下，`SimpleBusLT` 会对访问同一 target port 的 blocking `b_transport` transaction 做 target-level serialization，并记录 queue delay、target service delay 和 total delay。

这仍然不是完整 NoC 或真实互连模型；它是一个可运行、可回归、可继续演进的最小 bus contention / target serialization 基线。

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
- `start_time_ns`: transaction 的有效 bus 到达时间，即 `sc_time_stamp() + incoming t`。
- `delay_ns`: initiator 本次可见的总 delay；Phase 3 中等同于 `total_delay_ns`。
- `end_time_ns`: `start_time_ns + delay_ns`。
- `decoded_port`: `SimpleBusLT` 从原始 address 解码出的 target port。
- `masked_address`: `trans.set_address(...)` 后传给 target 的地址。
- `data_length`: `trans.get_data_length()`。
- `response_status`: target `b_transport` 返回后的 `trans.get_response_string()`。
- `request_time_ns`: `SimpleBusLT::initiatorBTransport()` 被调用时的原始 `sc_time_stamp()`。
- `bus_grant_time_ns`: 根据 target `busy_until` 计算出的服务开始时间。
- `queue_delay_ns`: bus/contention model 引入的等待时间。
- `target_service_delay_ns`: target `b_transport` 本身增加的 delay。
- `total_delay_ns`: `queue_delay_ns + target_service_delay_ns`。
- `target_busy_until_ns`: 本次 transaction 后该 target port 的下一次可服务时间。

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
rm -f examples/lt/results/latency_trace.csv
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

报告包含 overview、按 initiator/target/command 的 delay summary、queue delay summary、service/total delay breakdown、response status 统计、decoded port 统计、address range summary、data length summary、sanity checks，以及 first/last timeline rows。

## Current Modeling Meaning

当前模型由两部分组成：

- `target_service_delay_ns`: target-side annotated delay，来自 target accept delay 与 memory read/write response delay 的组合。
- `queue_delay_ns`: `SimpleBusLT` 中每个 target port 的 `busy_until` 状态产生的最小排队等待。

`delay_ns` 和 `total_delay_ns` 表示 initiator 本次可见的总 delay，即 `queue_delay_ns + target_service_delay_ns`。

当前配置下的 nominal target service delay：

| target_id | command | delay |
| --- | --- | --- |
| 201 | READ | 120 ns |
| 201 | WRITE | 80 ns |
| 202 | READ | 60 ns |
| 202 | WRITE | 40 ns |

计算来源：

- target 201: accept `20 ns` + read `100 ns` / write `60 ns`
- target 202: accept `10 ns` + read `50 ns` / write `30 ns`

Initiator 101 和 102 当前是对称配置：它们使用相同的 base address 参数，通过同一个 `SimpleBusLT` 访问相同两个 target。Phase 3 的最小模型会让访问同一 target port 的 transaction 按 `busy_until` 串行化，因此可以在 trace 中看到由 target serialization 产生的 `queue_delay_ns`。

## Current Limitations

当前还没有实现：

- configurable arbitration policy
- full NoC contention model
- multi-stage queueing model
- bandwidth saturation
- outstanding transaction modeling
- AT/non-blocking timing path analysis

因此，当前报告适合验证 transaction 路径、target delay 配置、Renode bridge 接入、workload 分离，以及最小 target serialization 对 queue delay 的影响；不适合直接解释真实 SoC interconnect 的复杂拥塞行为。

## Suggested Next Phase

Phase 4 建议把当前 `busy_until` 模型扩展为可配置 arbitration / bandwidth model。

目标是在保持 blocking LT 路径可回归的前提下，引入更明确的 arbitration policy、每 target/service bandwidth 参数，以及对 head-of-line blocking 和 tail latency 的更细粒度统计。
