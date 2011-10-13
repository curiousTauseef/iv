#ifndef IV_LV5_JSARRAY_H_
#define IV_LV5_JSARRAY_H_
#include <cstdlib>
#include <limits>
#include <vector>
#include <set>
#include <algorithm>
#include "conversions.h"
#include "lv5/error_check.h"
#include "lv5/gc_template.h"
#include "lv5/property.h"
#include "lv5/jsval.h"
#include "lv5/jsobject.h"
#include "lv5/map.h"
#include "lv5/slot.h"
#include "lv5/class.h"
#include "lv5/context_utils.h"
#include "lv5/object_utils.h"
#include "lv5/adapter/select1st.h"
#include "lv5/railgun/fwd.h"
namespace iv {
namespace lv5 {
namespace detail {

static bool IsDefaultDescriptor(const PropertyDescriptor& desc) {
  // only accept
  // { enumrable: true, configurable: true, writable:true }
  if (!desc.IsEnumerable()) {
    return false;
  }
  if (!desc.IsConfigurable()) {
    return false;
  }
  if (desc.IsAccessorDescriptor()) {
    return false;
  }
  if (desc.IsDataDescriptor()) {
    const DataDescriptor* const data = desc.AsDataDescriptor();
    return data->IsWritable();
  }
  return false;
}

static bool IsAbsentDescriptor(const PropertyDescriptor& desc) {
  if (!desc.IsEnumerable() && !desc.IsEnumerableAbsent()) {
    // explicitly not enumerable
    return false;
  }
  if (!desc.IsConfigurable() && !desc.IsConfigurableAbsent()) {
    // explicitly not configurable
    return false;
  }
  if (desc.IsAccessorDescriptor()) {
    return false;
  }
  if (desc.IsDataDescriptor()) {
    const DataDescriptor* const data = desc.AsDataDescriptor();
    return data->IsWritable() || data->IsWritableAbsent();
  }
  return true;
}

static DescriptorSlot::Data<uint32_t>
DescriptorToArrayLengthSlot(const PropertyDescriptor& desc) {
  assert(desc.IsDataDescriptor());
  const JSVal val = desc.AsDataDescriptor()->value();
  assert(val.IsNumber());
  const uint32_t res = val.GetUInt32();
  return DescriptorSlot::Data<uint32_t>(res, desc.attrs());
}

}  // namespace iv::lv5::detail

class Context;

class JSArray : public JSObject {
 public:
  friend class railgun::VM;
  typedef GCHashMap<uint32_t, JSVal>::type SparseArray;

  static const uint32_t kMaxVectorSize = 10000;

  uint32_t GetLength() const {
    return length_.value();
  }

  static JSArray* New(Context* ctx) {
    JSArray* const ary = new JSArray(ctx, 0);
    ary->set_cls(JSArray::GetClass());
    ary->set_prototype(context::GetClassSlot(ctx, Class::Array).prototype);
    return ary;
  }

  static JSArray* New(Context* ctx, uint32_t n) {
    JSArray* const ary = new JSArray(ctx, n);
    ary->set_cls(JSArray::GetClass());
    ary->set_prototype(context::GetClassSlot(ctx, Class::Array).prototype);
    return ary;
  }

  static JSArray* NewPlain(Context* ctx) {
    return new JSArray(ctx, 0);
  }

  static JSArray* NewPlain(Context* ctx, Map* map) {
    return new JSArray(ctx, map, 0);
  }

  static const Class* GetClass() {
    static const Class cls = {
      "Array",
      Class::Array
    };
    return &cls;
  }

  bool GetOwnPropertySlot(Context* ctx, Symbol name, Slot* slot) const {
    if (symbol::IsArrayIndexSymbol(name)) {
      slot->MakeUnCacheable();
      const uint32_t index = symbol::GetIndexFromSymbol(name);
      if (!dense_) {
        Slot slot2;
        if (JSObject::GetOwnPropertySlot(ctx, name, &slot2)) {
          *slot = slot2;
          return true;
        }
      }
      if (kMaxVectorSize > index) {
        // this target included in vector (if dense array)
        if (vector_.size() > index) {
          const JSVal& val = vector_[index];
          if (!val.IsEmpty()) {
            // current is target
            slot->set_descriptor(
                DataDescriptor(val, ATTR::W | ATTR::E | ATTR::C));
            return true;
          }
        }
      } else {
        // target is index and included in map
        if (map_) {
          const SparseArray::const_iterator it = map_->find(index);
          if (it != map_->end()) {
            // target is found
            slot->set_descriptor(
                DataDescriptor(it->second, ATTR::W | ATTR::E | ATTR::C));
            return true;
          }
        }
      }
      return false;
    }
    if (name == symbol::length()) {
      slot->set_descriptor(length_);
      return true;
    }
    return JSObject::GetOwnPropertySlot(ctx, name, slot);
  }

  bool DefineOwnProperty(Context* ctx,
                         Symbol name,
                         const PropertyDescriptor& desc,
                         bool th,
                         Error* e) {
    if (symbol::IsArrayIndexSymbol(name)) {
      // section 15.4.5.1 step 4
      return DefineArrayIndexProperty(ctx, name, desc, th, e);
    } else if (name == symbol::length()) {
      // section 15.4.5.1 step 3
      return DefineLengthProperty(ctx, desc, th, e);
    } else {
      // section 15.4.5.1 step 5
      return JSObject::DefineOwnProperty(ctx, name, desc, th, e);
    }
  }

  bool Delete(Context* ctx, Symbol name, bool th, Error* e) {
    if (symbol::IsArrayIndexSymbol(name)) {
      const uint32_t index = symbol::GetIndexFromSymbol(name);
      if (kMaxVectorSize > index) {
        if (vector_.size() > index) {
          JSVal& val = vector_[index];
          if (!val.IsEmpty()) {
            val = JSEmpty;
            return true;
          } else if (dense_) {
            return true;
          }
        }
      } else {
        if (map_) {
          const SparseArray::iterator it = map_->find(index);
          if (it != map_->end()) {
            map_->erase(it);
            return true;
          } else if (dense_) {
            return true;
          }
        } else if (dense_) {
          return true;
        }
      }
      return JSObject::Delete(ctx, name, th, e);
    }
    if (symbol::length() == name) {
      if (th) {
        e->Report(Error::Type, "delete failed");
      }
      return false;
    }
    return JSObject::Delete(ctx, name, th, e);
  }

  void GetOwnPropertyNames(Context* ctx,
                           std::vector<Symbol>* vec,
                           EnumerationMode mode) const {
    uint32_t index = 0;
    if (length_.IsEnumerable() || (mode == INCLUDE_NOT_ENUMERABLE)) {
      if (std::find(vec->begin(), vec->end(), symbol::length()) == vec->end()) {
        vec->push_back(symbol::length());
      }
    }
    for (JSVals::const_iterator it = vector_.begin(),
         last = vector_.end(); it != last; ++it, ++index) {
      if (!it->IsEmpty()) {
        const Symbol sym = symbol::MakeSymbolFromIndex(index);
        if (std::find(vec->begin(), vec->end(), sym) == vec->end()) {
          vec->push_back(sym);
        }
      }
    }
    if (map_) {
      for (SparseArray::const_iterator it = map_->begin(),
           last = map_->end(); it != last; ++it) {
        if (!it->second.IsEmpty()) {
          const Symbol sym = symbol::MakeSymbolFromIndex(it->first);
          if (std::find(vec->begin(), vec->end(), sym) == vec->end()) {
            vec->push_back(sym);
          }
        }
      }
    }
    JSObject::GetOwnPropertyNames(ctx, vec, mode);
  }

 private:
  JSArray(Context* ctx, uint32_t len)
    : JSObject(context::GetArrayMap(ctx)),
      vector_((len <= kMaxVectorSize) ? len : 4, JSEmpty),
      map_(NULL),
      dense_(true),
      length_(len, ATTR::WRITABLE) {
  }

  JSArray(Context* ctx, Map* map, uint32_t len)
    : JSObject(map),
      vector_((len <= kMaxVectorSize) ? len : 4, JSEmpty),
      map_(NULL),
      dense_(true),
      length_(len, ATTR::WRITABLE) {
  }

#define REJECT(str)\
  do {\
    if (th) {\
      e->Report(Error::Type, str);\
    }\
    return false;\
  } while (0)

  // section 15.4.5.1 step 4
  bool DefineArrayIndexProperty(Context* ctx,
                                Symbol name,
                                const PropertyDescriptor& desc,
                                bool th, Error* e) {
    // 15.4.5.1 step 4-a
    const uint32_t index = symbol::GetIndexFromSymbol(name);
    const uint32_t old_len = length_.value();
    // 15.4.5.1 step 4-b
    if (index >= old_len && !length_.IsWritable()) {
      REJECT("adding an element to the array"
             "which length is not writable is rejected");
    }

    // dense array optimization code
    Slot slot;
    const bool is_default_descriptor = detail::IsDefaultDescriptor(desc);
    const bool is_absent_descriptor = detail::IsAbsentDescriptor(desc);
    if ((is_default_descriptor ||
         (index < old_len && is_absent_descriptor)) &&
         (dense_ || !JSObject::GetOwnPropertySlot(ctx, name, &slot))) {
      if (kMaxVectorSize > index) {
        if (vector_.size() > index) {
          if (vector_[index].IsEmpty()) {
            if (is_default_descriptor) {
              if (desc.AsDataDescriptor()->IsValueAbsent()) {
                vector_[index] = JSUndefined;
              } else {
                vector_[index] = desc.AsDataDescriptor()->value();
              }
              return FixUpLength(old_len, index);
            }
            // through like
            //
            // var ary = new Array(10);
            // Object.defineProperty(ary, '0', { });
            //
          } else {
            if (desc.IsDataDescriptor() &&
                !desc.AsDataDescriptor()->IsValueAbsent()) {
              vector_[index] = desc.AsDataDescriptor()->value();
            }
            return FixUpLength(old_len, index);
          }
        } else {
          if (is_default_descriptor) {
            vector_.resize(index + 1, JSEmpty);
            if (desc.AsDataDescriptor()->IsValueAbsent()) {
              vector_[index] = JSUndefined;
            } else {
              vector_[index] = desc.AsDataDescriptor()->value();
            }
            return FixUpLength(old_len, index);
          }
        }
      } else {
        if (!map_) {
          map_ = new(GC)SparseArray;
          if (is_default_descriptor) {
            if (desc.AsDataDescriptor()->IsValueAbsent()) {
              (*map_)[index] = JSUndefined;
            } else {
              (*map_)[index] = desc.AsDataDescriptor()->value();
            }
            return FixUpLength(old_len, index);
          }
        } else {
          SparseArray::iterator it = map_->find(index);
          if (it != map_->end()) {
            if (desc.IsDataDescriptor() &&
                !desc.AsDataDescriptor()->IsValueAbsent()) {
              (*map_)[index] = desc.AsDataDescriptor()->value();
            }
            return FixUpLength(old_len, index);
          } else {
            if (is_default_descriptor) {
              if (desc.AsDataDescriptor()->IsValueAbsent()) {
                (*map_)[index] = JSUndefined;
              } else {
                (*map_)[index] = desc.AsDataDescriptor()->value();
              }
              return FixUpLength(old_len, index);
            }
          }
        }
      }
    }
    // 15.4.5.1 step 4-c
    const bool succeeded =
        JSObject::DefineOwnProperty(ctx, name, desc,
                                    false, IV_LV5_ERROR_WITH(e, false));
    // 15.4.5.1 step 4-d
    if (!succeeded) {
      REJECT("define own property failed");
    }

    // move state from dense array to not dense array
    dense_ = false;
    if (kMaxVectorSize > index) {
      if (vector_.size() > index) {
        vector_[index] = JSEmpty;
      }
    } else {
      if (map_) {
        const SparseArray::iterator it = map_->find(index);
        if (it != map_->end()) {
          map_->erase(it);
        }
      }
    }
    // 15.4.5.1 step 4-e, 4-f
    return FixUpLength(old_len, index);
  }

  // section 15.4.5.1 step 3
  bool DefineLengthProperty(Context* ctx,
                            const PropertyDescriptor& desc,
                            bool th, Error* e) {
    if (desc.IsDataDescriptor()) {
      const DataDescriptor* const data = desc.AsDataDescriptor();
      if (data->IsValueAbsent()) {
        // GenericDescriptor
        // changing attribute [[Writable]] or TypeError.
        // [[Value]] not changed.
        //
        // length value is always not empty, so use
        // GetDefineOwnPropertyResult
        bool returned = false;
        if (IsDefineOwnPropertyAccepted(length_, desc, th, &returned, e)) {
          length_ =
              detail::DescriptorToArrayLengthSlot(
                  PropertyDescriptor::Merge(desc, length_));
        }
        return returned;
      }
      const double new_len_double =
          data->value().ToNumber(ctx, IV_LV5_ERROR_WITH(e, false));
      // length must be uint32_t
      const uint32_t new_len = core::DoubleToUInt32(new_len_double);
      if (new_len != new_len_double) {
        e->Report(Error::Range, "invalid array length");
        return false;
      }
      DataDescriptor new_len_desc(JSVal::UInt32(new_len), data->attrs());
      uint32_t old_len = length_.value();
      if (new_len >= old_len) {
        bool returned = false;
        if (IsDefineOwnPropertyAccepted(length_,
                                        new_len_desc, th, &returned, e)) {
          length_ =
              detail::DescriptorToArrayLengthSlot(
                  PropertyDescriptor::Merge(new_len_desc, length_));
        }
        return returned;
      }
      if (!length_.IsWritable()) {
        REJECT("\"length\" not writable");
      }
      const bool new_writable =
          new_len_desc.IsWritableAbsent() || new_len_desc.IsWritable();
      // 15.4.5.1 step 3-i
      if (!new_writable) {
        new_len_desc.set_writable(true);
      }
      bool succeeded = false;
      if (IsDefineOwnPropertyAccepted(length_,
                                      new_len_desc, th, &succeeded, e)) {
        length_ =
            detail::DescriptorToArrayLengthSlot(
                PropertyDescriptor::Merge(new_len_desc, length_));
      }
      if (!succeeded) {
        return false;
      }

      if (new_len < old_len) {
        if (dense_) {
          // dense array version
          CompactionToLength(new_len);
        } else if (old_len - new_len < (1 << 24)) {
          while (new_len < old_len) {
            old_len -= 1;
            // see Eratta
            const bool delete_succeeded =
                JSArray::Delete(ctx, symbol::MakeSymbolFromIndex(old_len),
                                false, IV_LV5_ERROR_WITH(e, false));
            if (!delete_succeeded) {
              new_len_desc.set_value(JSVal::UInt32(old_len + 1));
              if (!new_writable) {
                new_len_desc.set_writable(false);
              }
              bool wasted = false;
              if (IsDefineOwnPropertyAccepted(length_,
                                              new_len_desc,
                                              false, &wasted, e)) {
                length_ =
                    detail::DescriptorToArrayLengthSlot(
                        PropertyDescriptor::Merge(new_len_desc, length_));
              }
              IV_LV5_ERROR_GUARD_WITH(e, false);
              REJECT("shrink array failed");
            }
          }
        } else {
          std::vector<Symbol> keys;
          JSObject::GetOwnPropertyNames(ctx, &keys,
                                        JSObject::INCLUDE_NOT_ENUMERABLE);
          std::set<uint32_t> ix;
          for (std::vector<Symbol>::const_iterator it = keys.begin(),
               last = keys.end(); it != last; ++it) {
            if (symbol::IsArrayIndexSymbol(*it)) {
              ix.insert(symbol::GetIndexFromSymbol(*it));
            }
          }
          for (std::set<uint32_t>::const_reverse_iterator it = ix.rbegin(),
               last = ix.rend(); it != last; ++it) {
            if (*it < new_len) {
              break;
            }
            const bool delete_succeeded =
                Delete(ctx, symbol::MakeSymbolFromIndex(*it), false, e);
            if (!delete_succeeded) {
              const uint32_t result_len = *it + 1;
              CompactionToLength(result_len);
              new_len_desc.set_value(JSVal::UInt32(result_len));
              if (!new_writable) {
                new_len_desc.set_writable(false);
              }
              bool wasted = false;
              if (IsDefineOwnPropertyAccepted(length_,
                                              new_len_desc,
                                              false, &wasted, e)) {
                length_ =
                    detail::DescriptorToArrayLengthSlot(
                        PropertyDescriptor::Merge(new_len_desc, length_));
              }
              IV_LV5_ERROR_GUARD_WITH(e, false);
              REJECT("shrink array failed");
            }
          }
          CompactionToLength(new_len);
        }
      }
      if (!new_writable) {
        const DataDescriptor target = DataDescriptor(
            ATTR::UNDEF_ENUMERABLE |
            ATTR::UNDEF_CONFIGURABLE);
        bool wasted = false;
        if (IsDefineOwnPropertyAccepted(length_, target, false, &wasted, e)) {
          length_ =
              detail::DescriptorToArrayLengthSlot(
                  PropertyDescriptor::Merge(target, length_));
        }
      }
      return true;
    } else {
      // length is not configurable
      // so length is not changed
      bool returned = false;
      IsDefineOwnPropertyAccepted(length_, desc, th, &returned, e);
      return returned;
    }
  }

#undef REJECT

  void CompactionToLength(uint32_t length) {
    if (length > kMaxVectorSize) {
      if (map_) {
        std::vector<uint32_t> copy(map_->size());
        std::transform(map_->begin(), map_->end(),
                       copy.begin(),
                       adapter::select1st<SparseArray::value_type>());
        for (std::vector<uint32_t>::const_iterator it = copy.begin(),
             last = copy.end(); it != last; ++it) {
          if (*it >= length) {
            map_->erase(*it);
          }
        }
        if (map_->empty()) {
          map_ = NULL;
        }
      }
    } else {
      if (map_) {
        map_ = NULL;
      }
      if (vector_.size() > length) {
        vector_.resize(length, JSEmpty);
      }
    }
  }

  // use VM only
  //   ReservedNew
  //   Reserve
  //   Set
  static JSArray* ReservedNew(Context* ctx, uint32_t len) {
    JSArray* ary = New(ctx, len);
    ary->Reserve(len);
    return ary;
  }

  void Reserve(uint32_t len) {
    if (len > kMaxVectorSize) {
      // alloc map
      map_ = new(GC)SparseArray();
    }
  }

  void SetToVector(uint32_t index, const JSVal& val) {
    assert(kMaxVectorSize > index);
    vector_[index] = val;
  }

  void SetToMap(uint32_t index, const JSVal& val) {
    assert(kMaxVectorSize <= index);
    assert(map_);
    (*map_)[index] = val;
  }

  bool FixUpLength(uint32_t old_len, uint32_t index) {
    if (index >= old_len) {
      length_.set_value(index + 1);
    }
    return true;
  }

  JSVals vector_;
  SparseArray* map_;
  bool dense_;
  DescriptorSlot::Data<uint32_t> length_;
};

} }  // namespace iv::lv5
#endif  // IV_LV5_JSARRAY_H_
