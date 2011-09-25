#ifndef IV_LV5_RUNTIME_STRING_H_
#define IV_LV5_RUNTIME_STRING_H_
#include <cassert>
#include <vector>
#include <limits>
#include <utility>
#include "detail/tuple.h"
#include "ustring.h"
#include "ustringpiece.h"
#include "character.h"
#include "conversions.h"
#include "platform_math.h"
#include "lv5/error_check.h"
#include "lv5/constructor_check.h"
#include "lv5/arguments.h"
#include "lv5/match_result.h"
#include "lv5/jsval.h"
#include "lv5/error.h"
#include "lv5/jsstring.h"
#include "lv5/jsstringobject.h"
#include "lv5/jsregexp.h"
#include "lv5/runtime_regexp.h"

namespace iv {
namespace lv5 {
namespace runtime {
namespace detail {

static inline JSVal StringToStringValueOfImpl(const Arguments& args,
                                              const char* msg,
                                              Error* e) {
  const JSVal& obj = args.this_binding();
  if (!obj.IsString()) {
    if (obj.IsObject() && obj.object()->IsClass<Class::String>()) {
      return static_cast<JSStringObject*>(obj.object())->value();
    } else {
      e->Report(Error::Type, msg);
      return JSEmpty;
    }
  } else {
    return obj.string();
  }
}

static inline bool IsTrimmed(uint16_t c) {
  return core::character::IsWhiteSpace(c) ||
         core::character::IsLineTerminator(c);
}

inline int64_t SplitMatch(const JSString::Fiber* str,
                          uint32_t q,
                          const JSString::Fiber* rhs) {
  const std::size_t rs = rhs->size();
  const std::size_t s = str->size();
  if (q + rs > s) {
    return -1;
  }
  if (JSString::Fiber::traits_type::compare(rhs->data(),
                                            str->data() + q,
                                            rs) != 0) {
    return -1;
  }
  return q + rs;
}

inline JSVal StringSplit(Context* ctx,
                         const JSString* target,
                         const JSString* rstr, uint32_t lim, Error* e) {
  const uint32_t rsize = rstr->size();
  JSArray* const ary = JSArray::New(ctx);
  if (rsize == 0) {
    if (target->empty()) {
      // "".split("") => []
      return ary;
    } else {
      return target->Split(ctx, ary, lim, e);
    }
  } else if (rsize == 1) {
    return target->Split(ctx, ary, (*rstr->GetFiber())[0], lim, e);
  }
  const uint32_t size = target->size();
  const JSString::Fiber* fiber = target->GetFiber();
  uint32_t p = 0;
  uint32_t q = p;
  const JSString::Fiber* rhs = rstr->GetFiber();
  uint32_t length = 0;
  while (q != size) {
    const int64_t rs = detail::SplitMatch(fiber, q, rhs);
    if (rs == -1) {
      ++q;
    } else {
      const uint32_t end = static_cast<uint32_t>(rs);
      if (end == p) {
        ++q;
      } else {
        ary->DefineOwnProperty(
            ctx, symbol::MakeSymbolFromIndex(length),
            DataDescriptor(
                JSString::New(ctx, fiber->begin() + p, fiber->begin() + q),
                ATTR::W | ATTR::E | ATTR::C),
            false, e);
        ++length;
        if (length == lim) {
          return ary;
        }
        q = p = end;
      }
    }
  }
  ary->DefineOwnProperty(
      ctx, symbol::MakeSymbolFromIndex(length),
      DataDescriptor(
          JSString::New(ctx,
                        fiber->begin() + p,
                        fiber->begin() + size),
          ATTR::W | ATTR::E | ATTR::C),
      false, e);
  return ary;
}

inline regexp::MatchResult RegExpMatch(const JSString::Fiber* str,
                                       uint32_t q,
                                       const JSRegExp& reg,
                                       regexp::PairVector* vec) {
  vec->clear();
  return reg.Match(*str, q, vec);
}

struct Replace {
  enum State {
    kNormal,
    kDollar,
    kDigit,
    kDigitZero
  };
};

template<typename T>
class Replacer : private core::Noncopyable<> {
 public:
  template<typename Builder>
  void Replace(Builder* builder, Error* e) {
    using std::get;
    const regexp::MatchResult res = RegExpMatch(str_fiber_, 0, reg_, &vec_);
    if (get<2>(res)) {
      builder->Append(str_fiber_->begin(), str_fiber_->begin() + get<0>(res));
      static_cast<T*>(this)->DoReplace(builder, res, e);
    }
    builder->Append(str_fiber_->begin() + get<1>(res), str_fiber_->end());
  }

  template<typename Builder>
  void ReplaceGlobal(Builder* builder, Error* e) {
    using std::get;
    int previous_index = 0;
    int not_matched_index = previous_index;
    const int size = str_fiber_->size();
    do {
      const regexp::MatchResult res = detail::RegExpMatch(str_fiber_,
                                                          previous_index,
                                                          reg_, &vec_);
      if (!get<2>(res)) {
        break;
      }
      builder->Append(str_fiber_->begin() + not_matched_index,
                      str_fiber_->begin() + get<0>(res));
      const int this_index = get<1>(res);
      not_matched_index = this_index;
      if (previous_index == this_index) {
        ++previous_index;
      } else {
        previous_index = this_index;
      }
      static_cast<T*>(this)->DoReplace(builder, res, IV_LV5_ERROR_VOID(e));
      if (previous_index > size || previous_index < 0) {
        break;
      }
    } while (true);
    builder->Append(str_fiber_->begin() + not_matched_index, str_fiber_->end());
  }

 protected:
  Replacer(JSString* str, const JSRegExp& reg)
    : str_(str),
      str_fiber_(str->GetFiber()),
      reg_(reg),
      vec_() {
  }

  JSString* str_;
  const JSString::Fiber* str_fiber_;
  const JSRegExp& reg_;
  regexp::PairVector vec_;
};

class StringReplacer : public Replacer<StringReplacer> {
 public:
  typedef StringReplacer this_type;
  typedef Replacer<this_type> super_type;
  StringReplacer(JSString* str,
                 const JSRegExp& reg,
                 const JSString& replace)
    : super_type(str, reg),
      replace_(replace),
      replace_fiber_(replace_.GetFiber()) {
  }

  template<typename Builder>
  void DoReplace(Builder* builder, const regexp::MatchResult& res, Error* e) {
    using std::get;
    Replace::State state = Replace::kNormal;
    uint16_t upper_digit_char = '\0';
    for (typename JSString::Fiber::const_iterator it = replace_fiber_->begin(),
         last = replace_fiber_->end(); it != last; ++it) {
      const uint16_t ch = *it;
      if (state == Replace::kNormal) {
        if (ch == '$') {
          state = Replace::kDollar;
        } else {
          builder->Append(ch);
        }
      } else if (state == Replace::kDollar) {
        switch (ch) {
          case '$':  // $$ pattern
            state = Replace::kNormal;
            builder->Append('$');
            break;

          case '&':  // $& pattern
            state = Replace::kNormal;
            builder->Append(str_fiber_->begin() + get<0>(res),
                            str_fiber_->begin() + get<1>(res));
            break;

          case '`':  // $` pattern
            state = Replace::kNormal;
            builder->Append(str_fiber_->begin(),
                            str_fiber_->begin() + get<0>(res));
            break;

          case '\'':  // $' pattern
            state = Replace::kNormal;
            builder->Append(str_fiber_->begin() + get<1>(res),
                            str_fiber_->end());
            break;

          default:
            if (core::character::IsDecimalDigit(ch)) {
              state = (ch == '0') ? Replace::kDigitZero : Replace::kDigit;
              upper_digit_char = ch;
            } else {
              state = Replace::kNormal;
              builder->Append('$');
              builder->Append(ch);
            }
        }
      } else if (state == Replace::kDigit) {  // twin digit pattern search
        if (core::character::IsDecimalDigit(ch)) {  // twin digit
          const std::size_t single_n = core::Radix36Value(upper_digit_char);
          const std::size_t n = single_n * 10 + core::Radix36Value(ch);
          if (vec_.size() >= n) {
            const regexp::PairVector::value_type& pair = vec_[n - 1];
            if (pair.first != -1 && pair.second != -1) {  // check undefined
              builder->Append(str_fiber_->begin() + pair.first,
                              str_fiber_->begin() + pair.second);
            }
          } else {
            // single digit pattern search
            if (vec_.size() >= single_n) {
              const regexp::PairVector::value_type& pair = vec_[single_n - 1];
              if (pair.first != -1 && pair.second != -1) {  // check undefined
                builder->Append(str_fiber_->begin() + pair.first,
                                str_fiber_->begin() + pair.second);
              }
            } else {
              builder->Append('$');
              builder->Append(upper_digit_char);
            }
            builder->Append(ch);
          }
        } else {
          const std::size_t n = core::Radix36Value(upper_digit_char);
          if (vec_.size() >= n) {
            const regexp::PairVector::value_type& pair = vec_[n - 1];
            if (pair.first != -1 && pair.second != -1) {  // check undefined
              builder->Append(str_fiber_->begin() + pair.first,
                              str_fiber_->begin() + pair.second);
            }
          } else {
            builder->Append('$');
            builder->Append(upper_digit_char);
          }
          builder->Append(ch);
        }
        state = Replace::kNormal;
      } else {  // twin digit pattern search
        assert(state == Replace::kDigitZero);
        if (core::character::IsDecimalDigit(ch)) {
          const std::size_t n =
              core::Radix36Value(upper_digit_char)*10 + core::Radix36Value(ch);
          if (vec_.size() >= n) {
            const regexp::PairVector::value_type& pair = vec_[n - 1];
            if (pair.first != -1 && pair.second != -1) {  // check undefined
              builder->Append(str_fiber_->begin() + pair.first,
                              str_fiber_->begin() + pair.second);
            }
          } else {
            builder->Append("$0");
            builder->Append(ch);
          }
        } else {
          // $0 is not used
          builder->Append("$0");
          builder->Append(ch);
        }
        state = Replace::kNormal;
      }
    }

    if (state == Replace::kDollar) {
      builder->Append('$');
    } else if (state == Replace::kDigit) {
      const std::size_t n = core::Radix36Value(upper_digit_char);
      if (vec_.size() >= n) {
        const regexp::PairVector::value_type& pair = vec_[n - 1];
        if (pair.first != -1 && pair.second != -1) {  // check undefined
          builder->Append(str_fiber_->begin() + pair.first,
                          str_fiber_->begin() + pair.second);
        }
      } else {
        builder->Append('$');
        builder->Append(upper_digit_char);
      }
    } else if (state == Replace::kDigitZero) {
      builder->Append("$0");
    }
  }

 private:
  const JSString& replace_;
  const JSString::Fiber* replace_fiber_;
};

class FunctionReplacer : public Replacer<FunctionReplacer> {
 public:
  typedef FunctionReplacer this_type;
  typedef Replacer<this_type> super_type;
  FunctionReplacer(JSString* str,
                   const JSRegExp& reg,
                   JSFunction* function,
                   Context* ctx)
    : super_type(str, reg),
      function_(function),
      ctx_(ctx) {
  }

  template<typename Builder>
  void DoReplace(Builder* builder, const regexp::MatchResult& res, Error* e) {
    using std::get;
    ScopedArguments a(ctx_, 3 + vec_.size(), IV_LV5_ERROR_VOID(e));
    a[0] = JSString::New(ctx_,
                         str_fiber_->begin() + get<0>(res),
                         str_fiber_->begin() + get<1>(res));
    std::size_t i = 1;
    for (regexp::PairVector::const_iterator it = vec_.begin(),
         last = vec_.end(); it != last; ++it, ++i) {
      if (it->first != -1 && it->second != -1) {  // check undefined
        a[i] = JSString::New(ctx_,
                             str_fiber_->begin() + it->first,
                             str_fiber_->begin() + it->second);
      }
    }
    a[i++] = get<0>(res);
    a[i++] = str_;
    const JSVal result = function_->Call(&a, JSUndefined, IV_LV5_ERROR_VOID(e));
    const JSString* const replaced_str =
        result.ToString(ctx_, IV_LV5_ERROR_VOID(e));
    builder->AppendJSString(*replaced_str);
  }

 private:
  JSFunction* function_;
  Context* ctx_;
};

template<typename Builder>
inline void ReplaceOnce(Builder* builder,
                        const JSString::Fiber* str,
                        const JSString& search_str,
                        JSString::Fiber::size_type loc,
                        const JSString::Fiber* replace_str) {
  Replace::State state = Replace::kNormal;
  for (JSString::Fiber::const_iterator it = replace_str->begin(),
       last = replace_str->end(); it != last; ++it) {
    const uint16_t ch = *it;
    if (state == Replace::kNormal) {
      if (ch == '$') {
        state = Replace::kDollar;
      } else {
        builder->Append(ch);
      }
    } else {
      assert(state == Replace::kDollar);
      switch (ch) {
        case '$':  // $$ pattern
          state = Replace::kNormal;
          builder->Append('$');
          break;

        case '&':  // $& pattern
          state = Replace::kNormal;
          builder->AppendJSString(search_str);
          break;

        case '`':  // $` pattern
          state = Replace::kNormal;
          builder->Append(str->begin(), str->begin() + loc);
          break;

        case '\'':  // $' pattern
          state = Replace::kNormal;
          builder->Append(str->begin() + loc + search_str.size(), str->end());
          break;

        default:
          state = Replace::kNormal;
          builder->Append('$');
          builder->Append(ch);
      }
    }
  }
  if (state == Replace::kDollar) {
    builder->Append('$');
  }
}

}  // namespace detail

// section 15.5.1
inline JSVal StringConstructor(const Arguments& args, Error* e) {
  if (args.IsConstructorCalled()) {
    JSString* str;
    if (!args.empty()) {
      str = args.front().ToString(args.ctx(), IV_LV5_ERROR(e));
    } else {
      str = JSString::NewEmptyString(args.ctx());
    }
    return JSStringObject::New(args.ctx(), str);
  } else {
    if (!args.empty()) {
      return args.front().ToString(args.ctx(), e);
    } else {
      return JSString::NewEmptyString(args.ctx());
    }
  }
}

// section 15.5.3.2 String.fromCharCode([char0 [, char1[, ...]]])
inline JSVal StringFromCharCode(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.fromCharCode", args, e);
  Context* const ctx = args.ctx();
  JSStringBuilder builder;
  for (Arguments::const_iterator it = args.begin(),
       last = args.end(); it != last; ++it) {
    const uint32_t ch = it->ToUInt32(ctx, IV_LV5_ERROR(e));
    builder.Append(ch);
  }
  return builder.Build(args.ctx());
}

// section 15.5.4.2 String.prototype.toString()
inline JSVal StringToString(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.toString", args, e);
  return detail::StringToStringValueOfImpl(
      args, "String.prototype.toString is not generic function", e);
}

// section 15.5.4.3 String.prototype.valueOf()
inline JSVal StringValueOf(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.valueOf", args, e);
  return detail::StringToStringValueOfImpl(
      args, "String.prototype.valueOf is not generic function", e);
}

// section 15.5.4.4 String.prototype.charAt(pos)
inline JSVal StringCharAt(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.charAt", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  const JSString* const str = val.ToString(args.ctx(), IV_LV5_ERROR(e));
  double position;
  if (!args.empty()) {
    position = args.front().ToNumber(args.ctx(), IV_LV5_ERROR(e));
    position = core::DoubleToInteger(position);
  } else {
    // undefined -> NaN -> 0
    position = 0;
  }
  if (position < 0 || position >= str->size()) {
    return JSString::NewEmptyString(args.ctx());
  } else {
    return JSString::NewSingle(args.ctx(),
                               str->GetAt(static_cast<std::size_t>(position)));
  }
}

// section 15.5.4.5 String.prototype.charCodeAt(pos)
inline JSVal StringCharCodeAt(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.charCodeAt", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  const JSString* const str = val.ToString(args.ctx(), IV_LV5_ERROR(e));
  double position;
  if (!args.empty()) {
    position = args.front().ToNumber(args.ctx(), IV_LV5_ERROR(e));
    position = core::DoubleToInteger(position);
  } else {
    // undefined -> NaN -> 0
    position = 0;
  }
  if (position < 0 || position >= str->size()) {
    return JSNaN;
  } else {
    return JSVal::UInt16(str->GetAt(core::DoubleToUInt32(position)));
  }
}

// section 15.5.4.6 String.prototype.concat([string1[, string2[, ...]]])
inline JSVal StringConcat(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.concat", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  const JSString* const str = val.ToString(args.ctx(), IV_LV5_ERROR(e));
  JSStringBuilder builder;
  builder.AppendJSString(*str);
  for (Arguments::const_iterator it = args.begin(),
       last = args.end(); it != last; ++it) {
    const JSString* const r = it->ToString(args.ctx(), IV_LV5_ERROR(e));
    builder.AppendJSString(*r);
  }
  return builder.Build(args.ctx());
}

// section 15.5.4.7 String.prototype.indexOf(searchString, position)
inline JSVal StringIndexOf(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.indexOf", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  const JSString* const str = val.ToString(args.ctx(), IV_LV5_ERROR(e));
  const JSString* search_str;
  // undefined -> NaN -> 0
  double position = 0;
  if (!args.empty()) {
    search_str = args.front().ToString(args.ctx(), IV_LV5_ERROR(e));
    if (args.size() > 1) {
      position = args[1].ToNumber(args.ctx(), IV_LV5_ERROR(e));
      position = core::DoubleToInteger(position);
    }
  } else {
    // undefined -> "undefined"
    search_str = JSString::NewAsciiString(args.ctx(), "undefined");
  }
  const std::size_t start = std::min(
      static_cast<std::size_t>(std::max(position, 0.0)), str->size());
  const core::UStringPiece base(*str->GetFiber());
  const core::UStringPiece::size_type loc =
      base.find(*search_str->GetFiber(), start);
  return (loc == core::UStringPiece::npos) ? -1.0 : loc;
}

// section 15.5.4.8 String.prototype.lastIndexOf(searchString, position)
inline JSVal StringLastIndexOf(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.lastIndexOf", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  const JSString* const str = val.ToString(args.ctx(), IV_LV5_ERROR(e));
  const JSString* search_str;
  std::size_t target = str->size();
  if (!args.empty()) {
    search_str = args[0].ToString(args.ctx(), IV_LV5_ERROR(e));
    // undefined -> NaN
    if (args.size() > 1) {
      const double position = args[1].ToNumber(args.ctx(), IV_LV5_ERROR(e));
      if (!core::IsNaN(position)) {
        const double integer = core::DoubleToInteger(position);
        if (integer < 0) {
          target = 0;
        } else if (integer < target) {
          target = static_cast<std::size_t>(integer);
        }
      }
    }
  } else {
    // undefined -> "undefined"
    search_str = JSString::NewAsciiString(args.ctx(), "undefined");
  }
  const core::UStringPiece base(*str->GetFiber());
  const core::UStringPiece::size_type loc =
      base.rfind(*search_str->GetFiber(), target);
  return (loc == core::UStringPiece::npos) ? -1.0 : loc;
}

// section 15.5.4.9 String.prototype.localeCompare(that)
inline JSVal StringLocaleCompare(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.localeCompare", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  const JSString* const str = val.ToString(args.ctx(), IV_LV5_ERROR(e));
  const JSString* that;
  if (!args.empty()) {
    that = args.front().ToString(args.ctx(), IV_LV5_ERROR(e));
  } else {
    that = JSString::NewAsciiString(args.ctx(), "undefined");
  }
  return str->GetFiber()->compare(*that->GetFiber());
}


// section 15.5.4.10 String.prototype.match(regexp)
inline JSVal StringMatch(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.match", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  Context* const ctx = args.ctx();
  JSString* const str = val.ToString(ctx, IV_LV5_ERROR(e));
  const uint32_t args_count = args.size();
  JSRegExp* regexp;
  if (args_count == 0 ||
      !args[0].IsObject() ||
      !args[0].object()->IsClass<Class::RegExp>()) {
    ScopedArguments a(ctx, 1, IV_LV5_ERROR(e));
    if (args_count == 0) {
      a[0] = JSUndefined;
    } else {
      a[0] = args[0];
    }
    const JSVal res = RegExpConstructor(a, IV_LV5_ERROR(e));
    assert(res.IsObject());
    regexp = static_cast<JSRegExp*>(res.object());
  } else {
    regexp = static_cast<JSRegExp*>(args[0].object());
  }
  const bool global = regexp->global();
  if (!global) {
    return regexp->Exec(ctx, str, e);
  }
  // step 8
  return regexp->ExecGlobal(ctx, str, e);
}

// section 15.5.4.11 String.prototype.replace(searchValue, replaceValue)
inline JSVal StringReplace(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.replace", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  Context* const ctx = args.ctx();
  JSString* const str = val.ToString(ctx, IV_LV5_ERROR(e));
  const uint32_t args_count = args.size();
  const bool search_value_is_regexp =
      args_count != 0 &&
      args[0].IsObject() && args[0].object()->IsClass<Class::RegExp>();

  JSString* search_str = NULL;

  if (search_value_is_regexp) {
    // searchValue is RegExp
    using std::get;
    const JSRegExp* reg = static_cast<JSRegExp*>(args[0].object());
    JSStringBuilder builder;
    if (args_count > 1 && args[1].IsCallable()) {
      JSFunction* const callable = args[1].object()->AsCallable();
      detail::FunctionReplacer replacer(str, *reg, callable, ctx);
      if (reg->global()) {
        replacer.ReplaceGlobal(&builder, IV_LV5_ERROR(e));
      } else {
        replacer.Replace(&builder, IV_LV5_ERROR(e));
      }
    } else {
      const JSString* replace_value;
      if (args_count > 1) {
        replace_value = args[1].ToString(ctx, IV_LV5_ERROR(e));
      } else {
        replace_value = JSString::NewAsciiString(args.ctx(), "undefined");
      }
      detail::StringReplacer replacer(str, *reg, *replace_value);
      if (reg->global()) {
        replacer.ReplaceGlobal(&builder, IV_LV5_ERROR(e));
      } else {
        replacer.Replace(&builder, IV_LV5_ERROR(e));
      }
    }
    return builder.Build(ctx);
  } else {
    if (args_count == 0) {
      search_str = JSString::NewAsciiString(args.ctx(), "undefined");
    } else {
      search_str = args[0].ToString(ctx, IV_LV5_ERROR(e));
    }
    const JSString::Fiber* fiber = str->GetFiber();
    const core::UStringPiece base(*fiber);
    const core::UStringPiece::size_type loc =
        base.find(*search_str->GetFiber(), 0);
    if (loc == core::UStringPiece::npos) {
      // not found
      return str;
    }
    // found pattern
    JSStringBuilder builder;
    builder.Append(fiber->begin(), fiber->begin() + loc);
    if (args_count > 1 && args[1].IsCallable()) {
      JSFunction* const callable = args[1].object()->AsCallable();
      ScopedArguments a(ctx, 3, IV_LV5_ERROR(e));
      a[0] = search_str;
      a[1] = static_cast<double>(loc);
      a[2] = str;
      const JSVal result = callable->Call(&a, JSUndefined, IV_LV5_ERROR(e));
      const JSString* const res = result.ToString(ctx, IV_LV5_ERROR(e));
      builder.AppendJSString(*res);
    } else {
      const JSString* replace_value;
      if (args_count > 1) {
        replace_value = args[1].ToString(ctx, IV_LV5_ERROR(e));
      } else {
        replace_value = JSString::NewAsciiString(args.ctx(), "undefined");
      }
      detail::ReplaceOnce(&builder,
                          fiber, *search_str,
                          loc, replace_value->GetFiber());
    }
    builder.Append(fiber->begin() + loc + search_str->size(), fiber->end());
    return builder.Build(ctx);
  }
}

// section 15.5.4.12 String.prototype.search(regexp)
inline JSVal StringSearch(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.search", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  Context* const ctx = args.ctx();
  JSString* const str = val.ToString(ctx, IV_LV5_ERROR(e));
  const uint32_t args_count = args.size();
  JSRegExp* regexp;
  if (args_count == 0) {
    regexp = JSRegExp::New(ctx);
  } else if (args[0].IsObject() &&
             args[0].object()->IsClass<Class::RegExp>()) {
    regexp = JSRegExp::New(
        ctx, static_cast<JSRegExp*>(args[0].object()));
  } else {
    ScopedArguments a(ctx, 1, IV_LV5_ERROR(e));
    a[0] = args[0];
    const JSVal res = RegExpConstructor(a, IV_LV5_ERROR(e));
    assert(res.IsObject());
    regexp = static_cast<JSRegExp*>(res.object());
  }
  const int last_index = regexp->LastIndex(ctx, IV_LV5_ERROR(e));
  regexp->SetLastIndex(ctx, 0, IV_LV5_ERROR(e));
  const JSVal result = regexp->Exec(ctx, str, e);
  regexp->SetLastIndex(ctx, last_index, IV_LV5_ERROR(e));
  if (!e) {
    return JSUndefined;
  }
  if (result.IsNull()) {
    return static_cast<double>(-1);
  } else {
    assert(result.IsObject());
    return result.object()->Get(ctx, context::Intern(ctx, "index"), e);
  }
}

// section 15.5.4.13 String.prototype.slice(start, end)
inline JSVal StringSlice(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.slice", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  Context* const ctx = args.ctx();
  const JSString* const str = val.ToString(ctx, IV_LV5_ERROR(e));
  const JSString::Fiber* fiber = str->GetFiber();
  const uint32_t len = fiber->size();
  uint32_t start;
  if (!args.empty()) {
    double relative_start = args.front().ToNumber(ctx, IV_LV5_ERROR(e));
    relative_start = core::DoubleToInteger(relative_start);
    if (relative_start < 0) {
      start = core::DoubleToUInt32(std::max<double>(relative_start + len, 0.0));
    } else {
      start = core::DoubleToUInt32(std::min<double>(relative_start, len));
    }
  } else {
    start = 0;
  }
  uint32_t end;
  if (args.size() > 1) {
    if (args[1].IsUndefined()) {
      end = len;
    } else {
      double relative_end = args[1].ToNumber(ctx, IV_LV5_ERROR(e));
      relative_end = core::DoubleToInteger(relative_end);
      if (relative_end < 0) {
        end = core::DoubleToUInt32(std::max<double>(relative_end + len, 0.0));
      } else {
        end = core::DoubleToUInt32(std::min<double>(relative_end, len));
      }
    }
  } else {
    end = len;
  }
  const uint32_t span = (end < start) ? 0 : end - start;
  return JSString::New(ctx,
                       fiber->begin() + start,
                       fiber->begin() + start + span);
}

// section 15.5.4.14 String.prototype.split(separator, limit)
inline JSVal StringSplit(const Arguments& args, Error* e) {
  using std::get;
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.split", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  Context* const ctx = args.ctx();
  JSString* const str = val.ToString(ctx, IV_LV5_ERROR(e));
  const uint32_t args_count = args.size();
  uint32_t lim;
  if (args_count < 2 || args[1].IsUndefined()) {
    lim = 4294967295UL;  // (1 << 32) - 1
  } else {
    lim = args[1].ToUInt32(ctx, IV_LV5_ERROR(e));
  }

  bool regexp = false;
  JSVal target = JSEmpty;
  JSVal separator;
  if (args_count > 0) {
    separator = args[0];
    if (separator.IsObject() && separator.object()->IsClass<Class::RegExp>()) {
      target = separator;
      regexp = true;
    } else {
      target = separator.ToString(ctx, IV_LV5_ERROR(e));
    }
  } else {
    separator = JSUndefined;
  }

  if (lim == 0) {
    return JSArray::New(ctx);
  }

  if (separator.IsUndefined()) {
    JSArray* const a = JSArray::New(ctx);
    a->DefineOwnProperty(
      ctx,
      symbol::MakeSymbolFromIndex(0u),
      DataDescriptor(str, ATTR::W | ATTR::E | ATTR::C),
      false, IV_LV5_ERROR(e));
    return a;
  }

  if (!regexp) {
    assert(target.IsString());
    return detail::StringSplit(ctx, str, target.string(), lim, e);
  }

  assert(target.IsObject());
  JSRegExp* const reg = static_cast<JSRegExp*>(target.object());
  JSArray* const ary = JSArray::New(ctx);
  regexp::PairVector cap;
  const uint32_t size = str->size();
  if (size == 0) {
    if (get<2>(detail::RegExpMatch(str->GetFiber(), 0, *reg, &cap))) {
      return ary;
    }
    ary->DefineOwnProperty(
        ctx,
        symbol::MakeSymbolFromIndex(0),
        DataDescriptor(str, ATTR::W | ATTR::E | ATTR::C),
        false, IV_LV5_ERROR(e));
    return ary;
  }

  const JSString::Fiber* fiber = str->GetFiber();
  uint32_t p = 0;
  uint32_t q = p;
  uint32_t start_match = 0;
  uint32_t length = 0;
  while (q != size) {
    const regexp::MatchResult rs = detail::RegExpMatch(fiber, q, *reg, &cap);
    if (!get<2>(rs) ||
        size == (start_match = get<0>(rs))) {
        break;
    }
    const uint32_t end = get<1>(rs);
    if (q == end && end == p) {
      ++q;
    } else {
      ary->DefineOwnProperty(
          ctx,
          symbol::MakeSymbolFromIndex(length),
          DataDescriptor(JSString::New(ctx,
                                       fiber->begin() + p,
                                       fiber->begin() + start_match),
                         ATTR::W | ATTR::E | ATTR::C),
          false, IV_LV5_ERROR(e));
      ++length;
      if (length == lim) {
        return ary;
      }

      uint32_t i = 0;
      for (regexp::PairVector::const_iterator it = cap.begin(),
           last = cap.end(); it != last; ++it) {
        ++i;
        if (it->first != -1 && it->second != -1) {
          ary->DefineOwnProperty(
              ctx,
              symbol::MakeSymbolFromIndex(length),
              DataDescriptor(
                  JSString::New(ctx,
                                fiber->begin() + it->first,
                                fiber->begin() + it->second),
                             ATTR::W | ATTR::E | ATTR::C),
              false, IV_LV5_ERROR(e));
        } else {
          ary->DefineOwnProperty(
              ctx,
              symbol::MakeSymbolFromIndex(length),
              DataDescriptor(JSUndefined,
                             ATTR::W | ATTR::E | ATTR::C),
              false, IV_LV5_ERROR(e));
        }
        ++length;
        if (length == lim) {
          return ary;
        }
      }
      q = p = end;
    }
  }
  ary->DefineOwnProperty(
      ctx,
      symbol::MakeSymbolFromIndex(length),
      DataDescriptor(
          JSString::New(ctx,
                        fiber->begin() + p,
                        fiber->begin() + size),
          ATTR::W | ATTR::E | ATTR::C),
      false, IV_LV5_ERROR(e));
  return ary;
}

// section 15.5.4.15 String.prototype.substring(start, end)
inline JSVal StringSubstring(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.substring", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  Context* const ctx = args.ctx();
  JSString* const str = val.ToString(ctx, IV_LV5_ERROR(e));
  const JSString::Fiber* fiber = str->GetFiber();
  const uint32_t len = fiber->size();
  uint32_t start;
  if (!args.empty()) {
    double integer = args.front().ToNumber(ctx, IV_LV5_ERROR(e));
    integer = core::DoubleToInteger(integer);
    start = core::DoubleToUInt32(
        std::min<double>(std::max<double>(integer, 0.0), len));
  } else {
    start = 0;
  }

  uint32_t end;
  if (args.size() > 1) {
    if (args[1].IsUndefined()) {
      end = len;
    } else {
      double integer = args[1].ToNumber(ctx, IV_LV5_ERROR(e));
      integer = core::DoubleToInteger(integer);
      end = core::DoubleToUInt32(
          std::min<double>(std::max<double>(integer, 0.0), len));
    }
  } else {
    end = len;
  }
  const uint32_t from = std::min<uint32_t>(start, end);
  const uint32_t to = std::max<uint32_t>(start, end);
  return JSString::New(ctx, fiber->begin() + from, fiber->begin() + to);
}

// section 15.5.4.16 String.prototype.toLowerCase()
inline JSVal StringToLowerCase(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.toLowerCase", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  JSString* const str = val.ToString(args.ctx(), IV_LV5_ERROR(e));
  const JSString::Fiber* fiber = str->GetFiber();
  JSStringBuilder builder;
  for (JSString::Fiber::const_iterator it = fiber->begin(),
       last = fiber->end(); it != last; ++it) {
    builder.Append(core::character::ToLowerCase(*it));
  }
  return builder.Build(args.ctx());
}

// section 15.5.4.17 String.prototype.toLocaleLowerCase()
inline JSVal StringToLocaleLowerCase(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.toLocaleLowerCase", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  JSString* const str = val.ToString(args.ctx(), IV_LV5_ERROR(e));
  const JSString::Fiber* fiber = str->GetFiber();
  JSStringBuilder builder;
  for (JSString::Fiber::const_iterator it = fiber->begin(),
       last = fiber->end(); it != last; ++it) {
    builder.Append(core::character::ToLowerCase(*it));
  }
  return builder.Build(args.ctx());
}

// section 15.5.4.18 String.prototype.toUpperCase()
inline JSVal StringToUpperCase(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.toUpperCase", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  JSString* const str = val.ToString(args.ctx(), IV_LV5_ERROR(e));
  const JSString::Fiber* fiber = str->GetFiber();
  JSStringBuilder builder;
  for (JSString::Fiber::const_iterator it = fiber->begin(),
       last = fiber->end(); it != last; ++it) {
    builder.Append(core::character::ToUpperCase(*it));
  }
  return builder.Build(args.ctx());
}

// section 15.5.4.19 String.prototype.toLocaleUpperCase()
inline JSVal StringToLocaleUpperCase(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.toLocaleUpperCase", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  JSString* const str = val.ToString(args.ctx(), IV_LV5_ERROR(e));
  const JSString::Fiber* fiber = str->GetFiber();
  JSStringBuilder builder;
  for (JSString::Fiber::const_iterator it = fiber->begin(),
       last = fiber->end(); it != last; ++it) {
    builder.Append(core::character::ToUpperCase(*it));
  }
  return builder.Build(args.ctx());
}

// section 15.5.4.20 String.prototype.trim()
inline JSVal StringTrim(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.trim", args, e);
  const JSVal& val = args.this_binding();
  val.CheckObjectCoercible(IV_LV5_ERROR(e));
  JSString* const str = val.ToString(args.ctx(), IV_LV5_ERROR(e));
  const JSString::Fiber* fiber = str->GetFiber();
  JSString::Fiber::const_iterator lit = fiber->begin();
  const JSString::Fiber::const_iterator last = fiber->end();
  // trim leading space
  bool empty = true;
  for (; lit != last; ++lit) {
    if (!detail::IsTrimmed(*lit)) {
      empty = false;
      break;
    }
  }
  if (empty) {
    return JSString::NewEmptyString(args.ctx());
  }
  // trim tailing space
  JSString::Fiber::const_reverse_iterator rit = fiber->rbegin();
  const JSString::Fiber::const_reverse_iterator rend(lit);
  for (; rit != rend; ++rit) {
    if (!detail::IsTrimmed(*rit)) {
      break;
    }
  }
  return JSString::New(args.ctx(), lit, rit.base());
}

// section B.2.3 String.prototype.substr(start, length)
// this method is deprecated.
inline JSVal StringSubstr(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("String.prototype.substr", args, e);
  const JSVal& val = args.this_binding();
  Context* const ctx = args.ctx();
  const JSString* const str = val.ToString(args.ctx(), IV_LV5_ERROR(e));
  const JSString::Fiber* fiber = str->GetFiber();
  const double len = fiber->size();

  double start;
  if (!args.empty()) {
    const double integer = args.front().ToNumber(ctx, IV_LV5_ERROR(e));
    start = core::DoubleToInteger(integer);
  } else {
    start = 0.0;
  }

  double length;
  if (args.size() > 1) {
    if (args[1].IsUndefined()) {
      length = std::numeric_limits<double>::infinity();
    } else {
      const double integer = args[1].ToNumber(ctx, IV_LV5_ERROR(e));
      length = core::DoubleToInteger(integer);
    }
  } else {
    length = std::numeric_limits<double>::infinity();
  }

  double result5;
  if (start > 0 || start == 0) {
    result5 = start;
  } else {
    result5 = std::max<double>(start + len, 0.0);
  }

  const double result6 = std::min<double>(std::max<double>(length, 0.0),
                                          len - result5);

  if (result6 <= 0) {
    return JSString::NewEmptyString(ctx);
  }

  const uint32_t capacity = core::DoubleToUInt32(result6);
  const uint32_t start_position = core::DoubleToUInt32(result5);
  return JSString::New(ctx,
                       fiber->begin() + start_position,
                       fiber->begin() + start_position + capacity);
}

} } }  // namespace iv::lv5::runtime
#endif  // IV_LV5_RUNTIME_STRING_H_
