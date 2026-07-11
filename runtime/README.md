# ThreadX C and C++ runtime port

This directory contains project-owned system runtime integration. It is kept
outside `src` so application code does not share a source tree with toolchain,
RTOS, or C library adaptation code.

## Ownership

- `threadx/` owns application-thread startup and the project ThreadX
  configuration.
- `libc/` owns Newlib reentrancy and locking, plus the FileX-backed POSIX
  syscall layer. Its `include/` directory contains compatibility headers that
  Newlib does not provide for this target.
- `libstdcxx/` owns the ThreadX gthread/TLS implementation and the libstdc++
  replacement headers. Toolchain-versioned GCC sources live under `vendor/`.
- `tests/` exercises the complete runtime boundary in Linux simulation and in
  the board-resident self-test image.

The intended dependency direction is `libstdc++ -> libc -> FileX`; ThreadX is
the execution backend shared by both library ports. FileX is not exposed as a
separate project runtime layer.
