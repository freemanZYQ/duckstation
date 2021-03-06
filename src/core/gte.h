#pragma once
#include "common/state_wrapper.h"
#include "gte_types.h"

namespace CPU {
class Core;

namespace Recompiler {
class CodeGenerator;
}
} // namespace CPU

namespace GTE {

class Core
{
public:
  friend CPU::Core;
  friend CPU::Recompiler::CodeGenerator;

  Core();
  ~Core();

  void Initialize();
  void Reset();
  bool DoState(StateWrapper& sw);

  // control registers are offset by +32
  u32 ReadRegister(u32 index) const;
  void WriteRegister(u32 index, u32 value);

  void ExecuteInstruction(Instruction inst);

private:
  static constexpr s64 MAC0_MIN_VALUE = -(INT64_C(1) << 31);
  static constexpr s64 MAC0_MAX_VALUE = (INT64_C(1) << 31) - 1;
  static constexpr s64 MAC123_MIN_VALUE = -(INT64_C(1) << 43);
  static constexpr s64 MAC123_MAX_VALUE = (INT64_C(1) << 43) - 1;
  static constexpr s32 IR0_MIN_VALUE = 0x0000;
  static constexpr s32 IR0_MAX_VALUE = 0x1000;
  static constexpr s32 IR123_MIN_VALUE = -(INT64_C(1) << 15);
  static constexpr s32 IR123_MAX_VALUE = (INT64_C(1) << 15) - 1;

  // Checks for underflow/overflow.
  template<u32 index>
  void CheckMACOverflow(s64 value);

  // Checks for underflow/overflow, sign-extending to 31/43 bits.
  template<u32 index>
  s64 SignExtendMACResult(s64 value);

  template<u32 index>
  void TruncateAndSetMAC(s64 value, u8 shift);

  template<u32 index>
  void TruncateAndSetMACAndIR(s64 value, u8 shift, bool lm);

  template<u32 index>
  void TruncateAndSetIR(s32 value, bool lm);

  template<u32 index>
  u32 TruncateRGB(s32 value);

  void SetOTZ(s32 value);
  void PushSXY(s32 x, s32 y);
  void PushSZ(s32 value);
  void PushRGBFromMAC();

  // Divide using Unsigned Newton-Raphson algorithm.
  u32 UNRDivide(u32 lhs, u32 rhs);

  // 3x3 matrix * 3x1 vector, updates MAC[1-3] and IR[1-3]
  void MulMatVec(const s16 M[3][3], const s16 Vx, const s16 Vy, const s16 Vz, u8 shift, bool lm);

  // 3x3 matrix * 3x1 vector with translation, updates MAC[1-3] and IR[1-3]
  void MulMatVec(const s16 M[3][3], const s32 T[3], const s16 Vx, const s16 Vy, const s16 Vz, u8 shift, bool lm);

  // Interpolate colour, or as in nocash "MAC+(FC-MAC)*IR0".
  void InterpolateColor(s64 in_MAC1, s64 in_MAC2, s64 in_MAC3, u8 shift, bool lm);

  void RTPS(const s16 V[3], u8 shift, bool lm, bool last);
  void NCS(const s16 V[3], u8 shift, bool lm);
  void NCCS(const s16 V[3], u8 shift, bool lm);
  void NCDS(const s16 V[3], u8 shift, bool lm);
  void DPCS(const u8 color[3], u8 shift, bool lm);

  void Execute_MVMVA(Instruction inst);
  void Execute_SQR(Instruction inst);
  void Execute_OP(Instruction inst);
  void Execute_RTPS(Instruction inst);
  void Execute_RTPT(Instruction inst);
  void Execute_NCLIP(Instruction inst);
  void Execute_AVSZ3(Instruction inst);
  void Execute_AVSZ4(Instruction inst);
  void Execute_NCS(Instruction inst);
  void Execute_NCT(Instruction inst);
  void Execute_NCCS(Instruction inst);
  void Execute_NCCT(Instruction inst);
  void Execute_NCDS(Instruction inst);
  void Execute_NCDT(Instruction inst);
  void Execute_CC(Instruction inst);
  void Execute_CDP(Instruction inst);
  void Execute_DPCS(Instruction inst);
  void Execute_DPCT(Instruction inst);
  void Execute_DCPL(Instruction inst);
  void Execute_INTPL(Instruction inst);
  void Execute_GPL(Instruction inst);
  void Execute_GPF(Instruction inst);

  Regs m_regs = {};
};

#include "gte.inl"

} // namespace GTE