# Nucleo-H753ZI C++ Template

A modern, CMake-based C++ application template for the **STMicroelectronics NUCLEO-H753ZI** development board, using Eclipse ThreadX and the STM32H7 Hardware Abstraction Layer (HAL).

This repository provides a lean starting point for embedded C++ development with a small handwritten application layer on top of CubeMX-generated board support.

## Software Dependencies

Ensure you have the following installed on your host machine:

* [CMake](https://cmake.org/download/) (v3.22+)
* [Ninja](https://ninja-build.org/)
* [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
* [OpenOCD](https://openocd.org/)

## Build

Configure and build with the provided preset:

```sh
cmake --preset debug
cmake --build --preset debug
```

In VS Code, the workspace also exposes a `Build Debug` task.

## Flash and Debug

Use OpenOCD to flash and debug the application:

```sh
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg
```

In VS Code, the workspace also exposes a `Flash` task.