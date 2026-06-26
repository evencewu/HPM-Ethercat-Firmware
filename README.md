# hpm5e00_can_demo / FoE Slave Demo

本项目目前包含两个可独立构建的应用：

1. **CAN 2.0 Echo Demo**（原始）：`src/mcan_echo.c`
2. **EtherCAT FoE Slave Demo**（新增）：`src/esc_soes/main.c`，使用 SOES 协议栈

当前默认构建的是 **EtherCAT FoE Slave Demo**，可通过 EtherCAT 接收固件，写入 Flash 暂存区后搬运到应用区并自动启动新固件。

详细测试流程见 [TESTING.md](TESTING.md)。

## 环境配置

``` bash
export HPM_SDK_BASE=/home/evence/code_file/dependence/hpm_sdk
export GNURISCV_TOOLCHAIN_PATH=/home/evence/code_file/dependence/rv32imac_zicsr_zifencei_multilib_b_ext-linux
```

``` bash
ln -s /home/evence/code_file/dependence/riscv-openocd/src/openocd openocd
```

## 编译（FoE Slave）

``` bash
cmake -B build -S . -GNinja \
  -DBOARD=hpm5e00evk \
  -DCUSTOM_TARGET_TRIPLET=riscv32-unknown-elf \
  -DRV_ARCH=rv32imac_zicsr_zifencei \
  -DHPM_BUILD_TYPE=flash_xip

ninja -C build
```

输出：`build/output/demo.elf` / `build/output/demo.bin`

## 烧录

### openocd配置

* 一键烧录

``` bash
sudo ./openocd -s $HPM_SDK_BASE/boards/openocd \
    -f $HPM_SDK_BASE/boards/openocd/probes/ft2232.cfg \
    -f $HPM_SDK_BASE/boards/openocd/soc/hpm5e00.cfg \
    -f $HPM_SDK_BASE/boards/openocd/boards/hpm5e00evk.cfg \
    -c "init" \
    -c "halt" \
    -c "flash write_image erase build/output/demo.elf" \
    -c "verify_image build/output/demo.elf" \
    -c "exit"
```

## 通过 EtherCAT 更新固件（FoE）

在 PC 端构建 `soem_cpp` 的 `foe_flash` 工具：

``` bash
cd ~/code_file/test/soem_cpp
cmake -B build -S .
cmake --build build
```

### 闭环测试（主机端）

无需硬件即可运行的校验脚本：

``` bash
python3 tools/foe_loopback_check.py
```

该脚本会检查：
- 生成的固件二进制是否有合法的 RISC-V 复位向量
- SII EEPROM 是否宣告 CoE + FoE 邮箱协议
- BOOT 邮箱地址/大小是否与 SOES 配置一致
- 固件 CRC32 及 metadata 结构是否正确

### 硬件在环测试

1. 烧录 FoE Slave 固件到 HPM5E00EVK。
2. 用网线连接 HPM5E00EVK 的 EtherCAT 口到 PC 网卡（如 `enp5s0`）。
3. 查询从站信息：

``` bash
cd ~/code_file/dependence/SOEM/build
make slaveinfo
sudo ./samples/slaveinfo/slaveinfo enp5s0
```

4. 通过 FoE 烧录新固件：

``` bash
cd ~/code_file/test/soem_cpp
sudo ./build/foe_flash enp5s0 1 build/output/demo.bin
```

从站在传输完成后会校验 CRC32，搬运到应用区并复位启动新固件。

`enp5s0` 替换为实际网卡名。

## 回到 CAN Demo

编辑 `CMakeLists.txt`，注释掉 FoE Slave 相关源文件，恢复 CAN Echo 的源文件即可。

## 调试

启动 OpenOCD server:

``` bash
openocd -s $HPM_SDK_BASE/boards/openocd \
    -f $HPM_SDK_BASE/boards/openocd/probes/ft2232.cfg \
    -f $HPM_SDK_BASE/boards/openocd/soc/hpm5e00.cfg \
    -f $HPM_SDK_BASE/boards/openocd/boards/hpm5e00evk.cfg
```

GDB 连接:

``` bash
TOOLCHAIN=/home/evence/code_file/dependence/rv32imac_zicsr_zifencei_multilib_b_ext-linux
$TOOLCHAIN/bin/riscv32-unknown-elf-gdb \
    -ex "file build/output/demo.elf" \
    -ex "target remote localhost:3333" \
    -ex "load" \
    -ex "set \$pc=0" \
    -ex "continue"
```

monitor flash write_image erase build/output/demo.elf
monitor reset run
