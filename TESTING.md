# HPM5E00 FoE Slave 闭环测试文档

本文档描述如何对 `hpm5e_can`（从站固件）和 `soem_cpp`（主站工具）进行闭环测试，包括无需硬件的主机端校验和需要 HPM5E00EVK 的硬件在环测试。

## 测试目标

1. 从站固件能正确编译并生成有效的 RISC-V 镜像。
2. 从站的 SII EEPROM 数据正确宣告 EtherCAT 身份和 FoE 能力。
3. 主站工具 `foe_flash` 能正确构建并识别 FoE 能力。
4. 主站能通过 EtherCAT 把 `.bin` 固件下发到从站。
5. 从站接收完成后校验 CRC32，搬运到应用 Flash 区并启动新固件。

## 环境要求

- Linux PC（已验证 Ubuntu）
- `enp5s0` 或类似的以太网接口用于 EtherCAT
- HPM5E00EVK 开发板（硬件在环测试必需）
- OpenOCD 用于烧录从站固件
- 环境变量：
  ```bash
  export HPM_SDK_BASE=/home/evence/code_file/dependence/hpm_sdk
  export GNURISCV_TOOLCHAIN_PATH=/home/evence/code_file/dependence/rv32imac_zicsr_zifencei_multilib_b_ext-linux
  ```

## 目录约定

```
~/code_file/dependence/hpm5e_can   # 本从站项目
~/code_file/test/soem_cpp          # 主站工具项目
~/code_file/dependence/SOEM          # SOEM 源码
~/code_file/dependence/SOES          # SOES 源码（已克隆）
```

## 一、一键主机端校验（无需硬件）

### 快速方式

```bash
cd ~/code_file/dependence/hpm5e_can
python3 tools/run_closed_loop_test.py
```

该脚本会：
1. 构建 `hpm5e_can` 从站固件。
2. 构建 `soem_cpp` 的 `foe_flash`。
3. 构建 SOEM 的 `slaveinfo`。
4. 运行 `tools/foe_loopback_check.py` 检查固件和 SII。

### 分步方式

#### 1.1 构建从站固件

```bash
cd ~/code_file/dependence/hpm5e_can
rm -rf build
cmake -B build -S . -GNinja \
  -DBOARD=hpm5e00evk \
  -DCUSTOM_TARGET_TRIPLET=riscv32-unknown-elf \
  -DRV_ARCH=rv32imac_zicsr_zifencei \
  -DHPM_BUILD_TYPE=flash_xip
ninja -C build
```

成功标志：
- 生成 `build/output/demo.elf`
- 生成 `build/output/demo.bin`

#### 1.2 构建主站工具

```bash
cd ~/code_file/test/soem_cpp
rm -rf build
cmake -B build -S .
cmake --build build
```

成功标志：
- 生成 `build/foe_flash`
- 生成 `build/soem_cpp_example`

#### 1.3 构建 SOEM slaveinfo

```bash
cd ~/code_file/dependence/SOEM/build
make slaveinfo
```

成功标志：
- 生成 `samples/slaveinfo/slaveinfo`

#### 1.4 运行主机端校验脚本

```bash
cd ~/code_file/dependence/hpm5e_can
python3 tools/foe_loopback_check.py
```

期望输出：
```
[OK] Firmware size: 84416 bytes
[OK] First word (reset vector): 0xfcf90002
[OK] SII config checksum: 0xd7
[OK] SII Vendor ID:    0x00001337
[OK] SII Product Code: 0x12783456
[OK] SII Revision:     0x00000001
[OK] SII Mailbox protocol: 0x000c
[OK] BOOT RX mailbox: 0x1000 (128 bytes)
[OK] BOOT TX mailbox: 0x1080 (128 bytes)
[OK] CRC32 algorithm match: 0x7bd2df9d
[OK] Metadata round-trip verified
[OK] All host-side checks passed.
```

## 二、硬件在环测试

### 2.1 硬件连接

1. 用网线把 HPM5E00EVK 的 EtherCAT 口接到 PC 的以太网口（如 `enp5s0`）。
2. 用 USB 连接 HPM5E00EVK 的调试口到 PC（用于 OpenOCD 烧录和串口打印）。
3. 给 HPM5E00EVK 上电。

### 2.2 烧录 FoE Slave 固件

```bash
cd ~/code_file/dependence/hpm5e_can
./openocd -s $HPM_SDK_BASE/boards/openocd \
    -f $HPM_SDK_BASE/boards/openocd/probes/ft2232.cfg \
    -f $HPM_SDK_BASE/boards/openocd/soc/hpm5e00.cfg \
    -f $HPM_SDK_BASE/boards/openocd/boards/hpm5e00evk.cfg \
    -c "program build/output/demo.elf verify reset exit"
```

或者使用 Segger/J-Link 等工具烧录 `build/output/demo.bin` 到 Flash 起始地址 `0x00000000`。

### 2.3 打开串口观察从站输出

```bash
# 根据实际情况替换串口设备名
minicom -D /dev/ttyUSB1 -b 115200
```

预期看到：
```
HPM5E00 SOES FoE demo
```

### 2.4 主站发现从站

```bash
cd ~/code_file/dependence/SOEM/build
sudo ./samples/slaveinfo/slaveinfo enp5s0
```

成功标志：
- 发现 1 个从站
- Vendor ID: `0x00001337`
- Product Code: `0x12783456`
- Mailbox protocol 包含 FoE

### 2.5 通过 FoE 烧录新固件

```bash
cd ~/code_file/test/soem_cpp
sudo ./build/foe_flash enp5s0 1 ~/code_file/dependence/hpm5e_can/build/output/demo.bin
```

成功标志：
```
[OK] ecx_init succeeded on enp5s0.
[OK] Found 1 slave(s).
[OK] FoE supported (mbx_proto=0x0c).
[..] BOOT SM0 (rx): PSA=0x1000 Len=128
[..] BOOT SM1 (tx): PSA=0x1080 Len=128
[..] Slave confirmed BOOT state
[OK] FoE write complete: 84416 bytes transferred.
```

### 2.6 观察从站搬运与复位

串口应输出：
```
Entering BOOT state
BOOT: start updating
BOOT: flash complete, total 84416 bytes
BOOT: staged CRC32 = 0x7bd2df9d
BOOT: copying firmware to application area and reset
```

随后 MCU 复位，重新打印：
```
HPM5E00 SOES FoE demo
```

### 2.7 验证新固件运行

再次运行：
```bash
cd ~/code_file/dependence/SOEM/build
sudo ./samples/slaveinfo/slaveinfo enp5s0
```

应仍能发现从站，说明新固件已成功启动。

## 三、故障排查

### 3.1 `slaveinfo` 找不到从站

- 检查网线是否连接正确。
- 检查网卡名是否为 `enp5s0`，必要时替换为实际接口名。
- 检查从站固件是否已正确烧录并运行（串口是否有输出）。
- 检查是否以 root 运行。

### 3.2 `foe_flash` 提示不支持 FoE

- 检查 SII 中 mailbox protocol 是否为 `0x0c`（CoE + FoE）。
- 重新运行 `python3 tools/foe_loopback_check.py` 确认。

### 3.3 FoE 传输失败或 CRC 校验失败

- 检查主从邮箱大小是否一致（默认 128 字节）。
- 检查串口输出，确认从站是否进入 BOOT 状态。
- 检查 `foe_flash` 中的 `--password` 或文件名是否与从站配置一致（从站接受文件名 `app`，密码 0）。

### 3.4 搬运后无法启动

- 检查暂存区首字是否合法（不能全 0 或全 1）。
- 检查 CRC32 是否校验通过。
- 检查应用 Flash 区擦除/编程是否成功。

## 四、自动化测试脚本

推荐优先使用：

```bash
cd ~/code_file/dependence/hpm5e_can
python3 tools/run_closed_loop_test.py
```

该脚本会执行所有主机端检查，并提示你如何连接硬件继续后续测试。

## 五、测试通过标准

| 检查项 | 通过标准 |
|--------|----------|
| 从站固件编译 | `build/output/demo.elf` 和 `demo.bin` 生成，无错误无告警 |
| 主站工具编译 | `soem_cpp/build/foe_flash` 生成 |
| SOEM slaveinfo 编译 | `SOEM/build/samples/slaveinfo/slaveinfo` 生成 |
| 主机端校验 | `foe_loopback_check.py` 全部 `[OK]` |
| 从站发现 | `slaveinfo` 显示 1 个从站，Vendor ID 0x1337 |
| FoE 烧录 | `foe_flash` 报告 `FoE write complete` |
| 自动搬运启动 | 串口显示搬运完成并复位，复位后仍能被发现 |

## 六、相关文件

- 主机端校验脚本：`tools/foe_loopback_check.py`
- 一键测试脚本：`tools/run_closed_loop_test.py`
- 从站 FoE 应用：`src/esc_soes/`
- SOES HPM5E00 HAL：`~/code_file/dependence/SOES/soes/hal/hpm5e00/`
- 主站工具：`~/code_file/test/soem_cpp/tools/foe_flash.cpp`
