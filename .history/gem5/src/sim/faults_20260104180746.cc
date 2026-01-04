/*
 * Copyright (c) 2020 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
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

#include "sim/faults.hh"

#include <csignal>

#include "arch/generic/decoder.hh"
#include "base/logging.hh"
#include "cpu/base.hh"
#include "cpu/thread_context.hh"
#include "debug/Faults.hh"
#include "mem/page_table.hh"
#include "sim/full_system.hh"
#include "sim/process.hh"
#include "sim/system.hh"

namespace gem5
{

void
FaultBase::invoke(ThreadContext *tc, const StaticInstPtr &inst)
{
    panic_if(!FullSystem, "fault (%s) detected @ PC %s",
             name(), tc->pcState());
    DPRINTF(Faults, "Fault %s at PC: %s
", name(), tc->pcState());
}

void
UnimpFault::invoke(ThreadContext *tc, const StaticInstPtr &inst)
{
    panic("Unimpfault: %s", panicStr.c_str());
}

void
SESyscallFault::invoke(ThreadContext *tc, const StaticInstPtr &inst)
{
    // Move the PC forward since that doesn't happen automatically.
    std::unique_ptr<PCStateBase> pc(tc->pcState().clone());
    inst->advancePC(*pc);
    tc->pcState(*pc);

    tc->getSystemPtr()->workload->syscall(tc);
}

void
ReExec::invoke(ThreadContext *tc, const StaticInstPtr &inst)
{
    tc->pcState(tc->pcState());
}

void
SyscallRetryFault::invoke(ThreadContext *tc, const StaticInstPtr &inst)
{
    tc->pcState(tc->pcState());
}

void
GenericPageTableFault::invoke(ThreadContext *tc, const StaticInstPtr &inst)
{
    bool handled = false;
    if (!FullSystem) {
        Process *p = tc->getProcessPtr();
        handled = p->fixupFault(vaddr);
    }
    panic_if(!handled &&
            !tc->getSystemPtr()->trapToGdb(GDBSignal::SEGV, tc->contextId()),
             "Page table fault when accessing virtual address %#x
", vaddr);
}

void
GenericAlignmentFault::invoke(ThreadContext *tc, const StaticInstPtr &inst)
{
    panic_if(!tc->getSystemPtr()->trapToGdb(GDBSignal::SEGV, tc->contextId()),
             "Alignment fault when accessing virtual address %#x
", vaddr);
}

void GenericHtmFailureFault::invoke(ThreadContext *tc,
                                    const StaticInstPtr &inst)
{
    // reset decoder
    InstDecoder* dcdr = tc->getDecoderPtr();
    dcdr->reset();

    // restore transaction checkpoint
    const auto& checkpoint = tc->getHtmCheckpointPtr();
    assert(checkpoint);
    assert(checkpoint->valid());

    checkpoint->restore(tc, getHtmFailureFaultCause());

    // reset the global monitor
    tc->getIsaPtr()->globalClearExclusive();

    // send abort packet to ruby (in final breath)
    tc->htmAbortTransaction(htmUid, cause);
}

void
GhostSpeculationFault::invoke(ThreadContext *tc, const StaticInstPtr &inst)
{
    DPRINTF(Faults, "GhostSpeculationFault invoked at PC: %s, SNR: %u
",
            tc->pcState(), snrValue);

    /*
     * The GhostArch Micro-Replay mechanism is handled by the O3CPU pipeline.
     * This fault is raised during commit and signals the pipeline to:
     * 1. Squash this instruction and any subsequent in-flight instructions
     * 2. Re-schedule this instruction for immediate re-execution
     *
     * The actual Micro-Replay logic is implemented in the O3CPU's commit stage
     * which checks for this fault type and triggers the appropriate recovery.
     *
     * For now, we simply advance the PC to prevent deadlock (standard fault
     * behavior). The O3CPU's FaultHandler will intercept this fault and
     * initiate the Micro-Replay mechanism instead of a full pipeline flush.
     */
    tc->pcState(tc->pcState());
}

} // namespace gem5
