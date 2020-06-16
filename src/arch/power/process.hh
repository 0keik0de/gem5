/*
 * Copyright (c) 2007-2008 The Florida State University
 * Copyright (c) 2009 The University of Edinburgh
 * Copyright (c) 2020 IBM Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __POWER_PROCESS_HH__
#define __POWER_PROCESS_HH__

#include <string>
#include <vector>

#include "mem/page_table.hh"
#include "sim/process.hh"
#include "sim/syscall_abi.hh"

namespace Loader
{
class ObjectFile;
} // namespace Loader;

class PowerProcess : public Process
{
  protected:
    PowerProcess(ProcessParams * params, ::Loader::ObjectFile *objFile);

    void initState() override;

  public:
    template <typename IntType>
    void argsInit(int pageSize);

    struct SyscallABI : public GenericSyscallABI64
    {
        static const std::vector<int> ArgumentRegs;
    };
};

enum PowerHWCAPFeature {
    PPC_FEATURE_32 = ULL(1) << 31,            // Always set for powerpc64
    PPC_FEATURE_64 = ULL(1) << 30,            // Always set for powerpc64
    PPC_FEATURE_HAS_ALTIVEC = ULL(1) << 28,
    PPC_FEATURE_HAS_FPU = ULL(1) << 27,
    PPC_FEATURE_HAS_MMU = ULL(1) << 26,
    PPC_FEATURE_UNIFIED_CACHE = ULL(1) << 24,
    PPC_FEATURE_NO_TB = ULL(1) << 20,         // 601/403gx have no timebase
    PPC_FEATURE_POWER4 = ULL(1) << 19,        // POWER4 ISA 2.00
    PPC_FEATURE_POWER5 = ULL(1) << 18,        // POWER5 ISA 2.02
    PPC_FEATURE_POWER5_PLUS = ULL(1) << 17,   // POWER5+ ISA 2.03
    PPC_FEATURE_CELL_BE = ULL(1) << 16,       // CELL Broadband Engine
    PPC_FEATURE_BOOKE = ULL(1) << 15,         // ISA Category Embedded
    PPC_FEATURE_SMT = ULL(1) << 14,           // Simultaneous Multi-Threading
    PPC_FEATURE_ICACHE_SNOOP = ULL(1) << 13,
    PPC_FEATURE_ARCH_2_05 = ULL(1) << 12,     // ISA 2.05
    PPC_FEATURE_PA6T = ULL(1) << 11,          // PA Semi 6T Core
    PPC_FEATURE_HAS_DFP = ULL(1) << 10,       // Decimal FP Unit
    PPC_FEATURE_POWER6_EXT = ULL(1) << 9,     // P6 + mffgpr/mftgpr
    PPC_FEATURE_ARCH_2_06 = ULL(1) << 8,      // ISA 2.06
    PPC_FEATURE_HAS_VSX = ULL(1) << 7,        // P7 Vector Extension
    PPC_FEATURE_PSERIES_PERFMON_COMPAT = ULL(1) << 6,
    PPC_FEATURE_TRUE_LE = ULL(1) << 1,
    PPC_FEATURE_PPC_LE = ULL(1) << 0
};

enum PowerHWCAP2Feature {
    PPC_FEATURE2_ARCH_2_07 = ULL(1) << 31,    // ISA 2.07
    PPC_FEATURE2_HAS_HTM = ULL(1) << 30,      // Hardware Transactional Memory
    PPC_FEATURE2_HAS_DSCR = ULL(1) << 29,     // Data Stream Control Register
    PPC_FEATURE2_HAS_EBB = ULL(1) << 28,      // Event Base Branching
    PPC_FEATURE2_HAS_ISEL = ULL(1) << 27,     // Integer Select
    PPC_FEATURE2_HAS_TAR = ULL(1) << 26,      // Target Address Register
    PPC_FEATURE2_HAS_VCRYPTO = ULL(1) << 25,  // Vector AES category
    PPC_FEATURE2_HTM_NOSC = ULL(1) << 24,
    PPC_FEATURE2_ARCH_3_00 = ULL(1) << 23,    // ISA 3.0
    PPC_FEATURE2_HAS_IEEE128 = ULL(1) << 22,  // VSX IEEE Binary Float 128-bit
};

namespace GuestABI
{

template <>
struct Result<PowerProcess::SyscallABI, SyscallReturn>
{
    static void
    store(ThreadContext *tc, const SyscallReturn &ret)
    {
        if (ret.suppressed() || ret.needsRetry())
            return;

        PowerISA::Cr cr = tc->readMiscReg(PowerISA::MISCREG_CR);
        if (ret.successful()) {
            cr.cr0.so = 0;
        } else {
            cr.cr0.so = 1;
        }
        tc->setMiscReg(PowerISA::MISCREG_CR, cr);
        tc->setIntReg(PowerISA::ReturnValueReg, ret.encodedValue());
    }
};

} // namespace GuestABI

#endif // __POWER_PROCESS_HH__

