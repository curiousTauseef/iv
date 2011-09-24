#ifndef IV_LV5_AERO_RANGE_BUILDER_H_
#define IV_LV5_AERO_RANGE_BUILDER_H_
#include <vector>
#include <utility>
#include <algorithm>
#include "noncopyable.h"
#include "lv5/aero/range.h"
#include "lv5/aero/escape.h"
namespace iv {
namespace lv5 {
namespace aero {

class RangeBuilder : core::Noncopyable<RangeBuilder> {
 public:
  // range value is [start, last]
  typedef std::vector<Range> Ranges;

  void Clear() {
    ranges_.clear();
  }

  void AddRange(uint16_t start, uint16_t last) {
    ranges_.push_back(std::make_pair(start, last));
  }

  void AddOrEscaped(uint16_t escaped, uint16_t ch) {
    if (escaped == 0) {
      Add(ch);
    } else {
      AddEscape(escaped);
    }
  }

  void Add(uint16_t ch) {
    ranges_.push_back(std::make_pair(ch, ch));
  }

  const Ranges& GetEscapedRange(uint16_t ch) {
    Clear();
    AddEscape(ch);
    return Finish();
  }

  const Ranges& Finish() {
    if (ranges_.empty()) {
      return ranges_;
    }
    Ranges result;
    std::sort(ranges_.begin(), ranges_.end());
    Ranges::const_iterator it = ranges_.begin();
    const Ranges::const_iterator last = ranges_.end();
    Range current = *it;
    ++it;
    for (; it != last; ++it) {
      if ((current.second + 1) >= it->first) {
        current.second = std::max(current.second, it->second);
      } else {
        result.push_back(current);
        current = *it;
      }
    }
    result.push_back(current);
    ranges_.swap(result);
    return ranges_;
  }

  template<typename Iter>
  void AddInvertedEscapeRange(Iter it, Iter last) {
    uint16_t start = 0x0000;
    for (; it != last; ++it) {
      ranges_.push_back(std::make_pair(start, it->first - 1));
      start = it->second + 1;
    }
    ranges_.push_back(std::make_pair(start, 0xFFFF));
  }

  void AddEscape(uint16_t escaped) {
    switch (escaped) {
      case 'd': {
        ranges_.insert(ranges_.end(), kDigitRanges.begin(), kDigitRanges.end());
        break;
      }
      case 'D': {
        AddInvertedEscapeRange(kDigitRanges.begin(), kDigitRanges.end());
        break;
      }
      case 's': {
        ranges_.insert(ranges_.end(), kSpaceRanges.begin(), kSpaceRanges.end());
        break;
      }
      case 'S': {
        AddInvertedEscapeRange(kSpaceRanges.begin(), kSpaceRanges.end());
        break;
      }
      case 'w': {
        ranges_.insert(ranges_.end(), kWordRanges.begin(), kWordRanges.end());
        break;
      }
      case 'W': {
        AddInvertedEscapeRange(kWordRanges.begin(), kWordRanges.end());
        break;
      }
      case 'n': {
        ranges_.insert(ranges_.end(),
                       kLineTerminatorRanges.begin(),
                       kLineTerminatorRanges.end());
        break;
      }
      case '.': {
        AddInvertedEscapeRange(kLineTerminatorRanges.begin(),
                               kLineTerminatorRanges.end());
        break;
      }
    }
  }

  static bool IsValidRange(uint16_t start, uint16_t last) {
    return start <= last;
  }
 private:
  Ranges ranges_;
};

} } }  // namespace iv::lv5::aero
#endif  // IV_LV5_AERO_RANGE_BUILDER_H_