# Nucleo-H753ZI C++ Template

A modern, CMake-based C++ application template for the **STMicroelectronics NUCLEO-H753ZI** development board, using Eclipse ThreadX and the STM32H7 Hardware Abstraction Layer (HAL).

This repository provides a lean starting point for embedded C++ development with a small handwritten application layer on top of CubeMX-generated board support.

## Software Dependencies

Ensure you have the following installed on your host machine:

* [CMake](https://cmake.org/download/) (v3.22+)
* [Ninja](https://ninja-build.org/)
* [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
* [OpenOCD](https://openocd.org/)

## Linux Simulation & Testing

To accelerate development and enable continuous integration (CI) without requiring the physical Nucleo board, this project can be compiled and run natively as a simulated Linux application. 

This is achieved by swapping the bare-metal ARM port of ThreadX for the ThreadX Linux/POSIX port, allowing your RTOS threads to run directly as a standard desktop process.

### Build Presets

The project now exposes explicit presets for both targets:

* `debug-stm32`
* `release-stm32`
* `debug-linux`
* `release-linux`

### Build STM32 Firmware

```sh
cmake --preset debug-stm32
cmake --build --preset debug-stm32
```

The STM32 artifacts are generated in `build/debug-stm32/`.

### Build Linux Simulation

```sh
cmake --preset debug-linux
cmake --build --preset debug-linux
```

The Linux executable is generated in `build/debug-linux/`.

### Run Linux Simulation

```sh
./build/debug-linux/Application
```

### VS Code Tasks

The workspace keeps the embedded debug flow intact and adds a Linux build task:

* `Build Debug` configures and builds `debug-stm32`
* `Flash` programs `build/debug-stm32/Application.elf`
* `Build Debug Linux` configures and builds `debug-linux`
