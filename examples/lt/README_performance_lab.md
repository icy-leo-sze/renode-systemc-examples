# examples/lt Performance Modeling Lab

## What This Lab Is

`examples/lt` 现在是基于原始 Renode/SystemC LT 示例改造出来的一个小型
SystemC/TLM SoC performance modeling lab。

这个 lab 保留原来的 LT blocking transport 路径和 Renode bridge，同时加入
latency trace 和最小 target-level contention model。它的目标不是变成完整
NoC simulator，而是在一个可运行、可回归的示例里，让简单的体系结构性能现象
可以被观察和分析。

这是 lab 第一次开始像一个 performance modeling environment，而不只是一个
functional TLM example。

## Architecture

入口与顶层：

- `sc_main`: `examples/lt/systemc/src/main.cpp`
  - 读取 Renode bridge 的 address 和 port 参数。
  - 构造 `top top("top", renode_address, renode_port)`。
  - 调用 `sc_core::sc_start()`。
- `top` module:
  - 声明在 `examples/lt/systemc/include/top.h`。
  - 连线在 `examples/lt/systemc/src/top.cpp`。

核心组件：

- Bus: `SimpleBusLT<3, 2> m_bus`
  - 文件：`examples/lt/systemc/third-party/systemc-lt-example/SimpleBusLT.h`
  - 3 个 initiator-facing target socket：initiator 101、initiator 102、Renode bridge。
  - 2 个 target-facing initiator socket：target 201、target 202。
- Initiator 101/102:
  - 创建位置：`examples/lt/systemc/src/top.cpp`。
  - 类型：`initiator_top`，内部包含 `traffic_generator` 和 `lt_initiator`。
  - 当前 workload 通过构造参数配置。
- Target 201:
  - 创建位置：`examples/lt/systemc/src/top.cpp`。
  - 实例：`m_at_and_lt_target_1`。
  - 类型：`at_target_1_phase`。
  - delay 配置：accept `20 ns`，read response `100 ns`，write response `60 ns`。
- Target 202:
  - 创建位置：`examples/lt/systemc/src/top.cpp`。
  - 实例：`m_lt_target_2`。
  - 类型：`lt_target`。
  - delay 配置：accept `10 ns`，read response `50 ns`，write response `30 ns`。
- Renode bridge:
  - 成员：`renode_bridge m_renode_bridge`。
  - 声明位置：`examples/lt/systemc/include/top.h`。
  - 连线位置：`examples/lt/systemc/src/top.cpp`。
  - 接入点：`m_renode_bridge.initiator_socket(m_bus.target_socket[2])`。
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

Renode 侧 `sysbus` read/write 通过 Renode bridge 作为第三个 initiator 接入同一个
`SimpleBusLT`。

## Trace Fields

CSV 输出文件：

```text
examples/lt/results/latency_trace.csv
```

字段：

- `initiator_id`: 发起方 ID。`101` 和 `102` 是 SystemC traffic generator，`9002` 是 Renode bridge。
- `target_id`: 目标 ID。`201` 对应 `at_target_1_phase`，`202` 对应 `lt_target`。
- `command`: TLM command，当前为 `READ`、`WRITE` 或 `OTHER`。
- `address`: bus 地址 mask 前的原始 transaction address。
- `data`: payload 前 4 bytes 按 `uint32_t` 解析；不可用时为 `0`。
- `start_time_ns`: transaction 的有效 bus 到达时间。
- `delay_ns`: initiator 可见的 observed transaction delay。
- `end_time_ns`: `start_time_ns + delay_ns`。
- `decoded_port`: `SimpleBusLT` 解码出的 target port。
- `masked_address`: `trans.set_address(...)` 后 target 看到的地址。
- `data_length`: `trans.get_data_length()`。
- `response_status`: target `b_transport` 返回后的 response string。
- `request_time_ns`: 进入 `SimpleBusLT::initiatorBTransport()` 时的原始 `sc_time_stamp()`。
- `bus_grant_time_ns`: 根据 target `busy_until` 计算出的服务开始时间。
- `queue_delay_ns`: 共享 target 路径上的排队等待。
- `target_service_delay_ns`: target 本身增加的服务时间。
- `total_delay_ns`: `queue_delay_ns + target_service_delay_ns`。
- `target_busy_until_ns`: 本次 transaction 后该 target 下一次可服务时间。
- `workload_transaction_count`: 本次实验中每个启用 initiator 的 transaction count。
- `workload_address_stride`: 本次实验配置的地址步长。
- `workload_target_pattern`: SystemC 侧实际使用的 target pattern，例如 `current_default`、
  `target201_only` 或 `target202_only`。
- `workload_enable_initiator_101`: 本次实验是否启用 initiator 101。
- `workload_enable_initiator_102`: 本次实验是否启用 initiator 102。

## Phase 4: Target Contention Modeling

Phase 4 把原始 LT 示例推进成一个小型 SystemC/TLM performance modeling lab。

关键观测是：当两个 initiator 共享同一条 target 访问路径时，target service delay
不再等于 observed transaction delay。`delay_ns` 现在表示 initiator 看到的
transaction observed delay，而 trace 会把这部分 delay 拆成 target service cost
和 contention queueing cost：

```text
total_delay_ns = queue_delay_ns + target_service_delay_ns
delay_ns       = observed transaction delay
```

字段语义：

- `target_service_delay_ns`: target 本身引入的服务时间。
- `queue_delay_ns`: 共享 target 路径上的排队等待，由 contention 产生。
- `total_delay_ns`: 排队等待和 target 服务时间之和。
- `delay_ns`: initiator 可见的 observed transaction delay。

对于 target 201 的 `READ`，target service delay 是 `120 ns`，但 observed delay
可以达到 `240 ns`。额外的 `120 ns` 是由 contention 引入的 queue delay。

| 场景 | target_service_delay_ns | observed delay | queue_delay_ns |
| --- | ---: | ---: | ---: |
| target 201 READ | 120 ns | up to 240 ns | up to 120 ns |

### Architecture Insight

这个实验已经具备一个最小但有意义的 architecture-level observation：它可以把
target service cost 和 shared-resource contention cost 区分开。

当前模型仍然不是完整 NoC，不支持复杂 arbitration，不建模 bandwidth saturation，
也不支持 multiple outstanding transactions。它只是一个最小 target serialization
model，用来观察两个 initiator 访问同一个 target path 时如何相互影响。

## Phase 5: Workload Sweep and Architecture Experiment Runner

Phase 5 把 lab 从单次运行升级成可重复实验的 workload sweep lab。

新增脚本：

```text
examples/lt/tools/run_workload_sweep.py
```

这个脚本会自动运行多组 workload case。每个 case 会：

- 清理旧的 `examples/lt/results/latency_trace.csv`
- 写入短生命周期的 `examples/lt/results/workload_config.env`
- 运行 `renode-test examples/lt/lt.robot`
- 调用 `analyze_latency.py --fail-on-sanity`
- 校验 trace 是否符合该 case 的 initiator、target pattern 和 address stride 期望
- 保存该 case 的 trace、analysis 文本和 stdout/stderr
- 把关键指标写入总表 `examples/lt/results/sweep/summary.csv`

默认 sweep cases：

- `baseline_dual_initiator_current_default`
- `single_initiator_101_current_default`
- `dual_initiator_target201_hotspot`
- `dual_initiator_target202_hotspot`
- `dual_initiator_stride_16_current_default`

每个 case 的输出目录：

```text
examples/lt/results/sweep/<case_name>/
```

其中包含：

- `latency_trace.csv`
- `analysis.txt`
- `renode-test.stdout.txt`
- `renode-test.stderr.txt`
- `analysis.stderr.txt`
- `summary_metrics.csv`

总表：

```text
examples/lt/results/sweep/summary.csv
```

总表字段包括：

- `case_name`
- `status`
- `burst_count`
- `address_stride`
- `enable_initiator_101`
- `enable_initiator_102`
- `target_pattern`
- `total_transactions`
- `avg_delay_ns`
- `max_delay_ns`
- `avg_queue_delay_ns`
- `max_queue_delay_ns`
- `contention_ratio_pct`
- `avg_target_service_delay_ns`
- `error`

`summary.csv` 中的 `status=PASS` 表示该 case 的 `renode-test` 成功返回，并且
`analyze_latency.py --fail-on-sanity` 没有发现 sanity failure，同时 trace 内容符合该
case 的 workload 期望。命令不存在、路径错误、trace 缺失、summary metrics 缺字段、
sanity failure 或 workload 未真正生效都会让当前 case 记录为 `status=FAIL`，并把原因
写入 `error` 字段。

之所以使用 `workload_config.env`，是因为 `run_workload_sweep.py` 传给 `renode-test`
的环境变量不一定会继续传给 Renode 启动的 SystemC 子进程。SystemC `top.cpp` 会优先读
环境变量；如果子进程拿不到环境变量，就读取这个临时 config 文件。runner 会在每个 case
结束后删除该文件，避免污染普通 `lt.robot` 运行。

sweep summary 默认只分析 SystemC traffic generator，也就是 initiator `101` 和 `102`。
Renode bridge traffic `9002` 不进入默认 sweep 指标，避免把 ms/s 级 Renode 访问和
ns/us 级 SystemC workload 混在同一组架构指标里。

Phase 5 的架构意义是：同一个 binary 和同一个 Robot 回归入口可以被重复用于多组
workload 参数，从而比较 single initiator、dual initiator、target hotspot、
address stride 等因素对 queue delay 和 contention ratio 的影响。

## Expected Sweep Results

| case_name | target_pattern | initiators | total_transactions | avg_delay_ns | avg_queue_delay_ns | contention_ratio_pct | interpretation |
| --- | --- | --- | ---: | ---: | ---: | ---: | --- |
| baseline_dual_initiator_current_default | both/current_default | 101 + 102 | 128 | 146.250 | 71.250 | 93.750 | Balanced dual-initiator baseline across target 201 and target 202. |
| single_initiator_101_current_default | both/current_default | 101 only | 64 | 75.000 | 0.000 | 0.000 | Single-initiator baseline removes shared-target contention. |
| dual_initiator_target201_hotspot | target201 | 101 + 102 | 128 | 199.062 | 99.062 | 99.219 | Slow target 201 hotspot amplifies queueing delay. |
| dual_initiator_target202_hotspot | target202 | 101 + 102 | 128 | 99.531 | 49.531 | 99.219 | Fast target 202 hotspot has lower service and queueing cost. |
| dual_initiator_stride_16_current_default | both/current_default | 101 + 102 | 128 | 146.250 | 71.250 | 93.750 | Stride changes address spacing, but current model has no cache/bank locality effect yet. |

这个 sweep 已经能区分 single initiator、dual initiator、target 201 hotspot、
target 202 hotspot 和 stride control。`avg_delay_ns` 由 target service delay 和
queue delay 共同构成，Phase 5 的价值是把 target 本身服务成本与共享资源竞争成本拆开。
target 201 hotspot 比 target 202 hotspot 慢，是因为 target 201 的 service delay 更高。
stride 16 当前与 baseline 一样，这是一个有意保留的 control case，说明模型还没有引入
cache line、bank conflict、burst locality 或 page locality。当前 lab 仍然只是一个
minimal SystemC/TLM performance modeling lab，不是完整 NoC 模型。

## Workload Knobs

当前 `traffic_generator` 可以通过构造参数配置 workload。第一版刻意把配置留在
SystemC object graph 内，没有增加命令行解析。

支持的 knobs：

- `transaction_count` / `LT_BURST_COUNT`: 每个 initiator 生成的 transaction 数量。
  `LT_BURST_COUNT` 实际映射到每个 initiator 的 `transaction_count`，不是全局总数。
- `address_stride`: 生成 transaction 时的地址步长。
- `target_pattern`: `target201_only`、`target202_only`、`alternate_201_202` 或 `current_default`。
- `read_write_mode`: `write_then_read`、`read_only` 或 `write_only`。
- `initiator_start_offset_ns`: initiator 启动偏移，用于控制两个 initiator 的重叠程度。

默认配置尽量保持原始 `lt.robot` 行为：两个 initiator 都使用 `current_default`、
`write_then_read`、64 笔 transaction、4-byte address stride，以及 zero start offset。

Phase 5 通过环境变量覆盖其中一部分 workload 配置：

- `LT_BURST_COUNT`
- `LT_ADDRESS_STRIDE`
- `LT_ENABLE_INITIATOR_101`
- `LT_ENABLE_INITIATOR_102`
- `LT_TARGET_PATTERN`

`LT_TARGET_PATTERN` 当前支持：

- `both`: 当前等价于 `current_default` pattern，访问 target 201 和 target 202；
  它不是 `alternate_201_202`，也不表示严格交错访问。
- `target201`: 只访问 target 201
- `target202`: 只访问 target 202

如果没有设置这些环境变量，默认行为保持不变，`lt.robot` 仍然走原来的 baseline workload。
在 sweep runner 中，这些值也会写入 `examples/lt/results/workload_config.env`，用于
Renode 没有把环境变量继续传给 SystemC binary 的情况。

## How To Build And Run

Ubuntu 示例：

```bash
cd /home/leo/renode-systemc-examples/examples/lt

source /home/leo/tools/renode_1.16.1-dotnet_portable/renode-env

cmake -S . -B build -DCMAKE_PREFIX_PATH=/home/leo/local/systemc
make -C build -j"$(nproc)"

# Renode script 期望 examples/lt/bin/lt 指向编译出的 lt 可执行文件。
mkdir -p bin
ln -sf ../build/lt bin/lt

cd /home/leo/renode-systemc-examples
rm -f examples/lt/results/latency_trace.csv
renode-test examples/lt/lt.robot

python3 examples/lt/tools/analyze_latency.py \
  --trace examples/lt/results/latency_trace.csv
```

运行 Phase 5 sweep：

```bash
python3 examples/lt/tools/run_workload_sweep.py \
  --robot examples/lt/lt.robot \
  --output-dir examples/lt/results/sweep \
  --keep-going
```

如果 `renode-test` 不在 `PATH` 中，可以显式指定：

```bash
python3 examples/lt/tools/run_workload_sweep.py \
  --renode-test-cmd /home/leo/tools/renode_1.16.1-dotnet_portable/renode-test \
  --robot examples/lt/lt.robot \
  --output-dir examples/lt/results/sweep \
  --keep-going
```

`lt.robot` 当前包含两个 Renode 测试用例，分别通过 `sysbus WriteDoubleWord` /
`ReadDoubleWord` 访问 target 201 和 target 202 对应的地址区间。

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
- `--dedup-identical`: 去掉完全重复的 transaction rows。
- `--summary-csv-output <path>`: 输出一行 summary CSV，供 sweep runner 读取。
- `--fail-on-sanity`: 如果 sanity checks 失败，退出非 0；sweep runner 默认开启。

常用命令：

```bash
# 完整 trace
python3 examples/lt/tools/analyze_latency.py

# 只看 SystemC traffic generator
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

报告包含 `Contention Summary`、`avg_queue_delay_ns`、`max_queue_delay_ns`、
`contention_ratio_pct`、response status 统计、decoded port 统计、address range
summary、data length summary、sanity checks，以及 first/last timeline rows。

## Current Modeling Meaning

当前 nominal target service delay：

| target_id | command | target_service_delay_ns |
| --- | --- | ---: |
| 201 | READ | 120 ns |
| 201 | WRITE | 80 ns |
| 202 | READ | 60 ns |
| 202 | WRITE | 40 ns |

来源：

- target 201: accept `20 ns` + read `100 ns` / write `60 ns`
- target 202: accept `10 ns` + read `50 ns` / write `30 ns`

Initiator 101 和 102 当前默认配置对称：它们使用相同的 base address 参数，并通过同一个
`SimpleBusLT` 访问相同两个 target。

## Current Limitations

当前还没有实现：

- 完整 NoC contention model
- 复杂 arbitration policy
- multi-stage queueing
- bandwidth saturation
- outstanding transaction modeling
- AT/non-blocking timing path analysis

当前报告适合验证 transaction path、target delay 配置、Renode bridge traffic、
workload 分离，以及最小 target serialization 对 queue delay 的影响。它不应被解释为
完整 SoC interconnect performance model。

## Suggested Next Phase

下一阶段可以在不离开 blocking LT 路径的前提下，让 sweep cases 更容易扩展和复现实验。
可以考虑把 case matrix 移到独立配置文件，并进一步加入 tail latency、per-target
bandwidth 参数，以及更明确的 arbitration policy 对比。
