# hpm5e00_can_demo


## 编译

``` bash
export HPM_SDK_BASE=/Users/evence/code_file/hpm/hpm_sdk
```

* 构建烧录到flash
``` bash
cmake -B build -S . -GNinja -DBOARD=hpm5e00evk -DCUSTOM_TARGET_TRIPLET=riscv-none-elf -DRV_ARCH=rv32imac_zicsr_zifencei -DHPM_BUILD_TYPE=flash_xip
```

* 构建烧录到ram (调试)
``` bash
cmake -B build -S . -GNinja -DBOARD=hpm5e00evk -DCUSTOM_TARGET_TRIPLET=riscv-none-elf -DRV_ARCH=rv32imac_zicsr_zifencei
```

``` bash
ninja -C build
```

## 烧录

``` bash
export HPM_SDK_BASE=/Users/evence/code_file/hpm/hpm_sdk
```

* 一键烧录
``` bash
openocd -s $HPM_SDK_BASE/boards/openocd \
    -f $HPM_SDK_BASE/boards/openocd/probes/ft2232.cfg \
    -f $HPM_SDK_BASE/boards/openocd/soc/hpm5e00.cfg \
    -f $HPM_SDK_BASE/boards/openocd/boards/hpm5e00evk.cfg \
    -c "init; program build/output/demo.elf verify reset exit"
```

* 两个终端
``` bash
openocd -s $HPM_SDK_BASE/boards/openocd \
    -f $HPM_SDK_BASE/boards/openocd/probes/ft2232.cfg \
    -f $HPM_SDK_BASE/boards/openocd/soc/hpm5e00.cfg \
    -f $HPM_SDK_BASE/boards/openocd/boards/hpm5e00evk.cfg
```

``` bash
    telnet localhost 4444

    program build/output/demo.elf verify reset exit
```
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
TOOLCHAIN=/Users/evence/Library/xPacks/@xpack-dev-tools/riscv-none-elf-gcc/15.2.0-1.1/.content
$TOOLCHAIN/bin/riscv-none-elf-gdb \
    -ex "file build/output/demo.elf" \
    -ex "target remote localhost:3333" \
    -ex "load" \
    -ex "set \$pc=0" \
    -ex "continue"
```

monitor flash write_image erase build/output/demo.elf
monitor reset run