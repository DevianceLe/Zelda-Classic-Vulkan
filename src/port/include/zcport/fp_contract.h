/*
 * The same floating point results on ARM64 as on x64, for MSVC on ARM64.
 *
 * The game is built with /fp:fast, which lets MSVC fuse a*b+c into one
 * fused multiply-add where the CPU has one: ARM64 always does, x64 (without
 * /arch:AVX2) never. The fused instruction skips the product's rounding, so
 * positions, colours and script values could come out a step off from the
 * x64 build. With contraction off, every product is rounded on its own, as
 * x64 has always done; the rest of /fp:fast stays as it is on x64.
 *
 * Forced into the game, zcport and zqeditor sources on Windows ARM64 only
 * (src/port/CMakeLists.txt, /FI); GCC and Clang take -ffp-contract=off
 * instead. Written from scratch for zcport.
 */

#ifndef ZCPORT_FP_CONTRACT_H
#define ZCPORT_FP_CONTRACT_H

#if defined(_MSC_VER)
#pragma fp_contract (off)
#endif

#endif
