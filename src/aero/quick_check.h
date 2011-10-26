#ifndef IV_AERO_QUICK_CHECK_H_
#define IV_AERO_QUICK_CHECK_H_
#include "aero/quick_check_fwd.h"
#include "aero/compiler.h"
namespace iv {
namespace aero {

inline uint16_t QuickCheck::Emit(const ParsedData& data) {
  data.pattern()->Accept(this);
  if (!IsFailed()) {
    const uint16_t value = filter_.value();
    return value;
  }
  return 0;
}

inline void QuickCheck::Visit(Disjunction* dis) {
  for (Alternatives::const_iterator it = dis->alternatives().begin(),
       last = dis->alternatives().end(); it != last; ++it) {
    if (IsFailed()) {
      return;
    }
    (*it)->Accept(this);
  }
}

inline void QuickCheck::Visit(Alternative* alt) {
  for (Expressions::const_iterator it = alt->terms().begin(),
       last = alt->terms().end(); it != last; ++it) {
    if (IsFailed()) {
      return;
    }
    (*it)->Accept(this);
    return;
  }
}

inline void QuickCheck::Visit(HatAssertion* assertion) {
  Fail();
}

inline void QuickCheck::Visit(DollarAssertion* assertion) {
  Fail();
}

inline void QuickCheck::Visit(EscapedAssertion* assertion) {
  Fail();
}

inline void QuickCheck::Visit(DisjunctionAssertion* assertion) {
  Fail();
}

inline void QuickCheck::Visit(BackReferenceAtom* atom) {
  Fail();
}

inline void QuickCheck::Visit(CharacterAtom* atom) {
  const FilterCheck check(this);
  const uint16_t ch = atom->character();
  if (compiler_->IsIgnoreCase()) {
    const uint16_t uu = core::character::ToUpperCase(ch);
    const uint16_t lu = core::character::ToLowerCase(ch);
    if (!(uu == lu && uu == ch)) {
      if (uu == ch || lu == ch) {
        filter_.Add(uu);
        filter_.Add(lu);
      } else {
        filter_.Add(ch);
        filter_.Add(uu);
        filter_.Add(lu);
      }
      return;
    }
  }
  filter_.Add(ch);
}

inline void QuickCheck::Visit(RangeAtom* atom) {
  Fail();
}

inline void QuickCheck::Visit(DisjunctionAtom* atom) {
  Visit(atom->disjunction());
}

inline void QuickCheck::Visit(Quantifiered* atom) {
  Fail();
}

} }  // namespace iv::aero
#endif  // IV_AERO_QUICK_CHECK_H_
