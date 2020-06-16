/*
 * Copyright (c) 2007-2008 The Florida State University
 * Copyright (c) 2009 The University of Edinburgh
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

#include "arch/power/process.hh"

#include "arch/power/isa_traits.hh"
#include "arch/power/types.hh"
#include "base/loader/elf_object.hh"
#include "base/loader/object_file.hh"
#include "base/logging.hh"
#include "cpu/thread_context.hh"
#include "debug/Stack.hh"
#include "mem/page_table.hh"
#include "params/Process.hh"
#include "sim/aux_vector.hh"
#include "sim/process_impl.hh"
#include "sim/syscall_return.hh"
#include "sim/system.hh"

using namespace std;
using namespace PowerISA;

PowerProcess::PowerProcess(
        ProcessParams *params, ::Loader::ObjectFile *objFile)
    : Process(params,
              new EmulationPageTable(params->name, params->pid, PageBytes),
              objFile)
{
    fatal_if(params->useArchPT, "Arch page tables not implemented.");
    // Set up break point (Top of Heap)
    Addr brk_point = image.maxAddr();
    brk_point = roundUp(brk_point, PageBytes);

    Addr stack_base = 0xbf000000L;

    Addr max_stack_size = 8 * 1024 * 1024;

    // Set pointer for next thread stack.  Reserve 8M for main stack.
    Addr next_thread_stack_base = stack_base - max_stack_size;

    // Set up region for mmaps. For now, start at bottom of kuseg space.
    Addr mmap_end = 0x70000000L;

    memState = make_shared<MemState>(this, brk_point, stack_base,
                                     max_stack_size, next_thread_stack_base,
                                     mmap_end);
}

void
PowerProcess::initState()
{
    Process::initState();

    if (objFile->getArch() == ::Loader::Power)
        argsInit<uint32_t>(PageBytes);
    else
        argsInit<uint64_t>(PageBytes);
}

template <typename IntType>
void
PowerProcess::argsInit(int pageSize)
{
    int intSize = sizeof(IntType);
    ByteOrder byteOrder = objFile->getByteOrder();
    bool is64bit = (objFile->getArch() == ::Loader::Power64);
    bool isLittleEndian = (byteOrder == LittleEndianByteOrder);
    std::vector<AuxVector<IntType>> auxv;

    string filename;
    if (argv.size() < 1)
        filename = "";
    else
        filename = argv[0];

    //We want 16 byte alignment
    uint64_t align = 16;

    // load object file into target memory
    image.write(*initVirtMem);
    interpImage.write(*initVirtMem);

    //Setup the auxilliary vectors. These will already have endian conversion.
    //Auxilliary vectors are loaded only for elf formatted executables.
    auto *elfObject = dynamic_cast<::Loader::ElfObject *>(objFile);
    if (elfObject) {
        IntType features = PPC_FEATURE_32;

        // Check if running in 64-bit mode
        if (is64bit)
            features |= PPC_FEATURE_64;

        // Check if running in little endian mode
        if (isLittleEndian)
            features |= PPC_FEATURE_PPC_LE | PPC_FEATURE_TRUE_LE;

        //Bits which describe the system hardware capabilities
        //XXX Figure out what these should be
        auxv.emplace_back(M5_AT_HWCAP, features);
        //The system page size
        auxv.emplace_back(M5_AT_PAGESZ, PowerISA::PageBytes);
        //Frequency at which times() increments
        auxv.emplace_back(M5_AT_CLKTCK, 0x64);
        // For statically linked executables, this is the virtual address of
        // the program header tables if they appear in the executable image
        auxv.emplace_back(M5_AT_PHDR, elfObject->programHeaderTable());
        // This is the size of a program header entry from the elf file.
        auxv.emplace_back(M5_AT_PHENT, elfObject->programHeaderSize());
        // This is the number of program headers from the original elf file.
        auxv.emplace_back(M5_AT_PHNUM, elfObject->programHeaderCount());
        // This is the base address of the ELF interpreter; it should be
        // zero for static executables or contain the base address for
        // dynamic executables.
        auxv.emplace_back(M5_AT_BASE, getBias());
        //XXX Figure out what this should be.
        auxv.emplace_back(M5_AT_FLAGS, 0);
        //The entry point to the program
        auxv.emplace_back(M5_AT_ENTRY, objFile->entryPoint());
        //Different user and group IDs
        auxv.emplace_back(M5_AT_UID, uid());
        auxv.emplace_back(M5_AT_EUID, euid());
        auxv.emplace_back(M5_AT_GID, gid());
        auxv.emplace_back(M5_AT_EGID, egid());
        //Whether to enable "secure mode" in the executable
        auxv.emplace_back(M5_AT_SECURE, 0);
        //The address of 16 "random" bytes
        auxv.emplace_back(M5_AT_RANDOM, 0);
        //The filename of the program
        auxv.emplace_back(M5_AT_EXECFN, 0);
        //The string "v51" with unknown meaning
        auxv.emplace_back(M5_AT_PLATFORM, 0);
    }

    //Figure out how big the initial stack nedes to be

    // A sentry NULL void pointer at the top of the stack.
    int sentry_size = intSize;

    string platform = "v51";
    int platform_size = platform.size() + 1;

    // The aux vectors are put on the stack in two groups. The first group are
    // the vectors that are generated as the elf is loaded. The second group
    // are the ones that were computed ahead of time and include the platform
    // string.
    int aux_data_size = filename.size() + 1;

    const int numRandomBytes = 16;
    aux_data_size += numRandomBytes;

    int env_data_size = 0;
    for (int i = 0; i < envp.size(); ++i) {
        env_data_size += envp[i].size() + 1;
    }
    int arg_data_size = 0;
    for (int i = 0; i < argv.size(); ++i) {
        arg_data_size += argv[i].size() + 1;
    }

    int info_block_size =
        sentry_size + env_data_size + arg_data_size +
        aux_data_size + platform_size;

    //Each auxilliary vector is two 4 byte words
    int aux_array_size = intSize * 2 * (auxv.size() + 1);

    int envp_array_size = intSize * (envp.size() + 1);
    int argv_array_size = intSize * (argv.size() + 1);

    int argc_size = intSize;

    //Figure out the size of the contents of the actual initial frame
    int frame_size =
        info_block_size +
        aux_array_size +
        envp_array_size +
        argv_array_size +
        argc_size;

    //There needs to be padding after the auxiliary vector data so that the
    //very bottom of the stack is aligned properly.
    int partial_size = frame_size;
    int aligned_partial_size = roundUp(partial_size, align);
    int aux_padding = aligned_partial_size - partial_size;

    int space_needed = frame_size + aux_padding;

    Addr stack_min = memState->getStackBase() - space_needed;
    stack_min = roundDown(stack_min, align);

    memState->setStackSize(memState->getStackBase() - stack_min);

    // map memory
    memState->mapRegion(roundDown(stack_min, pageSize),
                        roundUp(memState->getStackSize(), pageSize), "stack");

    // map out initial stack contents
    IntType sentry_base = memState->getStackBase() - sentry_size;
    IntType aux_data_base = sentry_base - aux_data_size;
    IntType env_data_base = aux_data_base - env_data_size;
    IntType arg_data_base = env_data_base - arg_data_size;
    IntType platform_base = arg_data_base - platform_size;
    IntType auxv_array_base = platform_base - aux_array_size - aux_padding;
    IntType envp_array_base = auxv_array_base - envp_array_size;
    IntType argv_array_base = envp_array_base - argv_array_size;
    IntType argc_base = argv_array_base - argc_size;

    DPRINTF(Stack, "The addresses of items on the initial stack:\n");
    DPRINTF(Stack, "0x%x - aux data\n", aux_data_base);
    DPRINTF(Stack, "0x%x - env data\n", env_data_base);
    DPRINTF(Stack, "0x%x - arg data\n", arg_data_base);
    DPRINTF(Stack, "0x%x - platform base\n", platform_base);
    DPRINTF(Stack, "0x%x - auxv array\n", auxv_array_base);
    DPRINTF(Stack, "0x%x - envp array\n", envp_array_base);
    DPRINTF(Stack, "0x%x - argv array\n", argv_array_base);
    DPRINTF(Stack, "0x%x - argc \n", argc_base);
    DPRINTF(Stack, "0x%x - stack min\n", stack_min);

    // write contents to stack

    // figure out argc
    IntType argc = argv.size();
    IntType guestArgc = htog(argc, byteOrder);

    //Write out the sentry void *
    IntType sentry_NULL = 0;
    initVirtMem->writeBlob(sentry_base, &sentry_NULL, sentry_size);

    //Fix up the aux vectors which point to other data
    for (int i = auxv.size() - 1; i >= 0; i--) {
        if (auxv[i].type == M5_AT_PLATFORM) {
            auxv[i].val = platform_base;
            initVirtMem->writeString(platform_base, platform.c_str());
        } else if (auxv[i].type == M5_AT_EXECFN) {
            auxv[i].val = aux_data_base + numRandomBytes;
            initVirtMem->writeString(aux_data_base, filename.c_str());
        } else if (auxv[i].type == M5_AT_RANDOM) {
            auxv[i].val = aux_data_base;
        }
    }

    //Copy the aux stuff
    Addr auxv_array_end = auxv_array_base;
    for (const auto &aux: auxv) {
        initVirtMem->write(auxv_array_end, aux, byteOrder);
        auxv_array_end += sizeof(aux);
    }
    //Write out the terminating zeroed auxilliary vector
    const AuxVector<uint64_t> zero(0, 0);
    initVirtMem->write(auxv_array_end, zero);
    auxv_array_end += sizeof(zero);

    copyStringArray(envp, envp_array_base, env_data_base,
                    byteOrder, *initVirtMem);
    copyStringArray(argv, argv_array_base, arg_data_base,
                    byteOrder, *initVirtMem);

    initVirtMem->writeBlob(argc_base, &guestArgc, intSize);

    ThreadContext *tc = system->threads[contextIds[0]];

    //Set the stack pointer register
    tc->setIntReg(StackPointerReg, stack_min);

    //Set the machine status for a typical userspace
    Msr msr = 0;
    msr.sf = is64bit;
    msr.hv = 1;
    msr.ee = 1;
    msr.pr = 1;
    msr.me = 1;
    msr.ir = 1;
    msr.dr = 1;
    msr.ri = 1;
    msr.le = isLittleEndian;
    tc->setMiscReg(MISCREG_MSR, msr);

    auto pc = tc->pcState();
    pc.set(getStartPC());
    pc.byteOrder(byteOrder);
    tc->pcState(pc);

    //Align the "stack_min" to a page boundary.
    memState->setStackMin(roundDown(stack_min, pageSize));
}

const std::vector<int> PowerProcess::SyscallABI::ArgumentRegs = {
    ArgumentReg0,
    ArgumentReg1,
    ArgumentReg2,
    ArgumentReg3,
    ArgumentReg4,
    ArgumentReg5
};
