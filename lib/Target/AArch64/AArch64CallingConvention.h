//=== AArch64CallingConv.h - Custom Calling Convention Routines -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the custom routines for the AArch64 Calling Convention
// that aren't done by tablegen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64CALLINGCONVENTION_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64CALLINGCONVENTION_H

#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/Target/TargetInstrInfo.h"

namespace {
using namespace llvm;

static const uint16_t XRegList[] = {AArch64::X0, AArch64::X1, AArch64::X2,
                                    AArch64::X3, AArch64::X4, AArch64::X5,
                                    AArch64::X6, AArch64::X7};
static const uint16_t SRegList[] = {AArch64::S0, AArch64::S1, AArch64::S2,
                                    AArch64::S3, AArch64::S4, AArch64::S5,
                                    AArch64::S6, AArch64::S7};
static const uint16_t DRegList[] = {AArch64::D0, AArch64::D1, AArch64::D2,
                                    AArch64::D3, AArch64::D4, AArch64::D5,
                                    AArch64::D6, AArch64::D7};
static const uint16_t QRegList[] = {AArch64::Q0, AArch64::Q1, AArch64::Q2,
                                    AArch64::Q3, AArch64::Q4, AArch64::Q5,
                                    AArch64::Q6, AArch64::Q7};

static bool finishStackBlock(SmallVectorImpl<CCValAssign> &PendingMembers,
                             MVT LocVT, ISD::ArgFlagsTy &ArgFlags,
                             CCState &State, unsigned SlotAlign) {
  unsigned Size = LocVT.getSizeInBits() / 8;
  unsigned StackAlign = State.getMachineFunction()
                            .getSubtarget()
                            .getDataLayout()
                            ->getStackAlignment();
  unsigned Align = std::min(ArgFlags.getOrigAlign(), StackAlign);

  for (auto &It : PendingMembers) {
    It.convertToMem(State.AllocateStack(Size, std::max(Align, SlotAlign)));
    State.addLoc(It);
    SlotAlign = 1;
  }

  // All pending members have now been allocated
  PendingMembers.clear();
  return true;
}

/// The Darwin variadic PCS places anonymous arguments in 8-byte stack slots. An
/// [N x Ty] type must still be contiguous in memory though.
static bool CC_AArch64_Custom_Stack_Block(
      unsigned &ValNo, MVT &ValVT, MVT &LocVT, CCValAssign::LocInfo &LocInfo,
      ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  SmallVectorImpl<CCValAssign> &PendingMembers = State.getPendingLocs();

  // Add the argument to the list to be allocated once we know the size of the
  // block.
  PendingMembers.push_back(
      CCValAssign::getPending(ValNo, ValVT, LocVT, LocInfo));

  if (!ArgFlags.isInConsecutiveRegsLast())
    return true;

  return finishStackBlock(PendingMembers, LocVT, ArgFlags, State, 8);
}

/// Given an [N x Ty] block, it should be passed in a consecutive sequence of
/// registers. If no such sequence is available, mark the rest of the registers
/// of that type as used and place the argument on the stack.
static bool CC_AArch64_Custom_Block(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                    CCValAssign::LocInfo &LocInfo,
                                    ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  // Try to allocate a contiguous block of registers, each of the correct
  // size to hold one member.
  ArrayRef<uint16_t> RegList;
  if (LocVT.SimpleTy == MVT::i64)
    RegList = XRegList;
  else if (LocVT.SimpleTy == MVT::f32)
    RegList = SRegList;
  else if (LocVT.SimpleTy == MVT::f64)
    RegList = DRegList;
  else if (LocVT.SimpleTy == MVT::v2f64)
    RegList = QRegList;
  else {
    // Not an array we want to split up after all.
    return false;
  }

  SmallVectorImpl<CCValAssign> &PendingMembers = State.getPendingLocs();

  // Add the argument to the list to be allocated once we know the size of the
  // block.
  PendingMembers.push_back(
      CCValAssign::getPending(ValNo, ValVT, LocVT, LocInfo));

  if (!ArgFlags.isInConsecutiveRegsLast())
    return true;

  unsigned RegResult = State.AllocateRegBlock(RegList, PendingMembers.size());
  if (RegResult) {
    for (auto &It : PendingMembers) {
      It.convertToReg(RegResult);
      State.addLoc(It);
      ++RegResult;
    }
    PendingMembers.clear();
    return true;
  }

  // Mark all regs in the class as unavailable
  for (auto Reg : RegList)
    State.AllocateReg(Reg);

  const AArch64Subtarget &Subtarget = static_cast<const AArch64Subtarget &>(
      State.getMachineFunction().getSubtarget());
  unsigned SlotAlign = Subtarget.isTargetDarwin() ? 1 : 8;

  return finishStackBlock(PendingMembers, LocVT, ArgFlags, State, SlotAlign);
}

}

#endif
