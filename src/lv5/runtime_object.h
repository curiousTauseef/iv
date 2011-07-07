#ifndef _IV_LV5_RUNTIME_OBJECT_H_
#define _IV_LV5_RUNTIME_OBJECT_H_
#include <cstddef>
#include <vector>
#include <utility>
#include <string>
#include <algorithm>
#include "detail/array.h"
#include "lv5/error_check.h"
#include "lv5/constructor_check.h"
#include "lv5/arguments.h"
#include "lv5/jsval.h"
#include "lv5/jsstring.h"
#include "lv5/jsobject.h"
#include "lv5/jsarray.h"
#include "lv5/error.h"
#include "lv5/context_utils.h"
#include "lv5/context.h"
#include "lv5/internal.h"

namespace iv {
namespace lv5 {
namespace runtime {
namespace detail {

class IsEnumerable {
 public:
  template<typename T>
  inline bool operator()(const T& val) const {
    return val.second.IsEnumerable();
  }
};

inline void DefinePropertiesImpl(Context* ctx,
                                 JSObject* obj,
                                 JSObject* props, Error* e) {
  typedef std::vector<std::pair<Symbol, PropertyDescriptor> > Descriptors;
  Descriptors descriptors;
  std::vector<Symbol> keys;
  props->GetOwnPropertyNames(ctx, &keys, JSObject::kExcludeNotEnumerable);
  for (std::vector<Symbol>::const_iterator it = keys.begin(),
       last = keys.end(); it != last; ++it) {
    const JSVal desc_obj = props->Get(ctx, *it,
                                      IV_LV5_ERROR_VOID(e));
    const PropertyDescriptor desc =
        internal::ToPropertyDescriptor(ctx, desc_obj, IV_LV5_ERROR_VOID(e));
    descriptors.push_back(std::make_pair(*it, desc));
  }
  for (Descriptors::const_iterator it = descriptors.begin(),
       last = descriptors.end(); it != last; ++it) {
    obj->DefineOwnProperty(ctx, it->first, it->second,
                           true, IV_LV5_ERROR_VOID(e));
  }
}

}  // namespace detail

// section 15.2.1.1 Object([value])
// section 15.2.2.1 new Object([value])
inline JSVal ObjectConstructor(const Arguments& args, Error* e) {
  if (args.IsConstructorCalled()) {
    if (args.size() > 0) {
      const JSVal& val = args[0];
      if (val.IsObject()) {
        JSObject* const obj = val.object();
        if (obj->IsNativeObject()) {
          return obj;
        } else {
          // 15.2.2.1 step 1.a.ii
          // implementation dependent host object behavior
          return JSUndefined;
        }
      }
      if (val.IsString() ||
          val.IsBoolean() ||
          val.IsNumber()) {
        return val.ToObject(args.ctx(), e);
      }
      assert(val.IsNull() || val.IsUndefined());
    }
    return JSObject::New(args.ctx());
  } else {
    if (args.size() > 0) {
      const JSVal& val = args[0];
      if (val.IsNull() || val.IsUndefined()) {
        return JSObject::New(args.ctx());
      } else {
        return val.ToObject(args.ctx(), e);
      }
    } else {
      return JSObject::New(args.ctx());
    }
  }
}

// section 15.2.3.2 Object.getPrototypeOf(O)
inline JSVal ObjectGetPrototypeOf(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.getPrototypeOf", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject()) {
      JSObject* const obj = first.object()->prototype();
      if (obj) {
        return obj;
      } else {
        return JSNull;
      }
    }
  }
  e->Report(Error::Type,
            "Object.getPrototypeOf requires Object argument");
  return JSUndefined;
}

// section 15.2.3.3 Object.getOwnPropertyDescriptor(O, P)
inline JSVal ObjectGetOwnPropertyDescriptor(const Arguments& args,
                                            Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.getOwnPropertyDescriptor", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject()) {
      JSObject* const obj = first.object();
      Symbol name;
      if (args.size() > 1) {
        JSString* const str = args[1].ToString(args.ctx(), IV_LV5_ERROR(e));
        name = context::Intern(args.ctx(), str);
      } else {
        name = context::Intern(args.ctx(), "undefined");
      }
      const PropertyDescriptor desc = obj->GetOwnProperty(args.ctx(), name);
      return internal::FromPropertyDescriptor(args.ctx(), desc);
    }
  }
  e->Report(Error::Type,
            "Object.getOwnPropertyDescriptor requires Object argument");
  return JSUndefined;
}

// section 15.2.3.4 Object.getOwnPropertyNames(O)
inline JSVal ObjectGetOwnPropertyNames(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.getOwnPropertyNames", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject()) {
      JSObject* const obj = first.object();
      JSArray* const ary = JSArray::New(args.ctx());
      Context* const ctx = args.ctx();
      uint32_t n = 0;
      std::vector<Symbol> keys;
      obj->GetOwnPropertyNames(ctx, &keys, JSObject::kIncludeNotEnumerable);
      for (std::vector<Symbol>::const_iterator it = keys.begin(),
           last = keys.end(); it != last; ++it, ++n) {
        ary->DefineOwnPropertyWithIndex(
            args.ctx(), n,
            DataDescriptor(ctx->ToString(*it),
                           PropertyDescriptor::WRITABLE |
                           PropertyDescriptor::ENUMERABLE |
                           PropertyDescriptor::CONFIGURABLE),
            false, IV_LV5_ERROR(e));
      }
      return ary;
    }
  }
  e->Report(Error::Type,
            "Object.getOwnPropertyNames requires Object argument");
  return JSUndefined;
}

// section 15.2.3.5 Object.create(O[, Properties])
inline JSVal ObjectCreate(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.create", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject() || first.IsNull()) {
      JSObject* const res = JSObject::New(args.ctx());
      if (first.IsObject()) {
        JSObject* const obj = first.object();
        res->set_prototype(obj);
      } else {
        res->set_prototype(NULL);
      }
      if (args.size() > 1 && !args[1].IsUndefined()) {
        JSObject* const props = args[1].ToObject(args.ctx(), IV_LV5_ERROR(e));
        detail::DefinePropertiesImpl(args.ctx(), res, props, IV_LV5_ERROR(e));
      }
      return res;
    }
  }
  e->Report(Error::Type,
            "Object.create requires Object or Null argument");
  return JSUndefined;
}

// section 15.2.3.6 Object.defineProperty(O, P, Attributes)
inline JSVal ObjectDefineProperty(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.defineProperty", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject()) {
      JSObject* const obj = first.object();
      Symbol name;
      if (args.size() > 1) {
        const JSString* const str =
            args[1].ToString(args.ctx(), IV_LV5_ERROR(e));
        name = context::Intern(args.ctx(), str);
      } else {
        name = context::Intern(args.ctx(), "undefined");
      }
      JSVal attr = JSUndefined;
      if (args.size() > 2) {
        attr = args[2];
      }
      const PropertyDescriptor desc =
          internal::ToPropertyDescriptor(args.ctx(), attr, IV_LV5_ERROR(e));
      obj->DefineOwnProperty(args.ctx(), name, desc, true, IV_LV5_ERROR(e));
      return obj;
    }
  }
  e->Report(Error::Type,
            "Object.defineProperty requires Object argument");
  return JSUndefined;
}

// section 15.2.3.7 Object.defineProperties(O, Properties)
inline JSVal ObjectDefineProperties(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.defineProperties", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject()) {
      JSObject* const obj = first.object();
      if (args.size() > 1) {
        JSObject* const props = args[1].ToObject(args.ctx(), IV_LV5_ERROR(e));
        detail::DefinePropertiesImpl(args.ctx(), obj, props, IV_LV5_ERROR(e));
        return obj;
      } else {
        // raise TypeError
        JSVal(JSUndefined).ToObject(args.ctx(), IV_LV5_ERROR(e));
        return JSUndefined;
      }
    }
  }
  e->Report(Error::Type,
            "Object.defineProperties requires Object argument");
  return JSUndefined;
}

// section 15.2.3.8 Object.seal(O)
inline JSVal ObjectSeal(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.seal", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject()) {
      JSObject* const obj = first.object();
      std::vector<Symbol> keys;
      Context* const ctx = args.ctx();
      obj->GetOwnPropertyNames(ctx, &keys, JSObject::kIncludeNotEnumerable);
      for (std::vector<Symbol>::const_iterator it = keys.begin(),
           last = keys.end(); it != last; ++it) {
        PropertyDescriptor desc = obj->GetOwnProperty(ctx, *it);
        if (desc.IsConfigurable()) {
          desc.set_configurable(false);
        }
        obj->DefineOwnProperty(
            ctx, *it, desc, true, IV_LV5_ERROR(e));
      }
      obj->set_extensible(false);
      return obj;
    }
  }
  e->Report(Error::Type,
            "Object.seal requires Object argument");
  return JSUndefined;
}

// section 15.2.3.9 Object.freeze(O)
inline JSVal ObjectFreeze(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.freeze", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject()) {
      JSObject* const obj = first.object();
      std::vector<Symbol> keys;
      Context* const ctx = args.ctx();
      obj->GetOwnPropertyNames(ctx, &keys, JSObject::kIncludeNotEnumerable);
      for (std::vector<Symbol>::const_iterator it = keys.begin(),
           last = keys.end(); it != last; ++it) {
        PropertyDescriptor desc = obj->GetOwnProperty(ctx, *it);
        if (desc.IsDataDescriptor()) {
          desc.AsDataDescriptor()->set_writable(false);
        }
        if (desc.IsConfigurable()) {
          desc.set_configurable(false);
        }
        obj->DefineOwnProperty(
            ctx, *it, desc, true, IV_LV5_ERROR(e));
      }
      obj->set_extensible(false);
      return obj;
    }
  }
  e->Report(Error::Type,
            "Object.freeze requires Object argument");
  return JSUndefined;
}

// section 15.2.3.10 Object.preventExtensions(O)
inline JSVal ObjectPreventExtensions(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.preventExtensions", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject()) {
      JSObject* const obj = first.object();
      obj->set_extensible(false);
      return obj;
    }
  }
  e->Report(Error::Type,
            "Object.preventExtensions requires Object argument");
  return JSUndefined;
}

// section 15.2.3.11 Object.isSealed(O)
inline JSVal ObjectIsSealed(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.isSealed", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject()) {
      JSObject* const obj = first.object();
      std::vector<Symbol> keys;
      Context* const ctx = args.ctx();
      obj->GetOwnPropertyNames(ctx, &keys, JSObject::kIncludeNotEnumerable);
      for (std::vector<Symbol>::const_iterator it = keys.begin(),
           last = keys.end(); it != last; ++it) {
        const PropertyDescriptor desc = obj->GetOwnProperty(ctx, *it);
        if (desc.IsConfigurable()) {
          return JSFalse;
        }
      }
      return JSVal::Bool(!obj->IsExtensible());
    }
  }
  e->Report(Error::Type,
            "Object.isSealed requires Object argument");
  return JSUndefined;
}

// section 15.2.3.12 Object.isFrozen(O)
inline JSVal ObjectIsFrozen(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.isFrozen", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject()) {
      JSObject* const obj = first.object();
      std::vector<Symbol> keys;
      Context* const ctx = args.ctx();
      obj->GetOwnPropertyNames(ctx, &keys, JSObject::kIncludeNotEnumerable);
      for (std::vector<Symbol>::const_iterator it = keys.begin(),
           last = keys.end(); it != last; ++it) {
        const PropertyDescriptor desc = obj->GetOwnProperty(ctx, *it);
        if (desc.IsDataDescriptor()) {
          if (desc.AsDataDescriptor()->IsWritable()) {
            return JSFalse;
          }
        }
        if (desc.IsConfigurable()) {
          return JSFalse;
        }
      }
      return JSVal::Bool(!obj->IsExtensible());
    }
  }
  e->Report(Error::Type,
            "Object.isFrozen requires Object argument");
  return JSUndefined;
}

// section 15.2.3.13 Object.isExtensible(O)
inline JSVal ObjectIsExtensible(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.isExtensible", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject()) {
      JSObject* const obj = first.object();
      return JSVal::Bool(obj->IsExtensible());
    }
  }
  e->Report(Error::Type,
            "Object.isExtensible requires Object argument");
  return JSUndefined;
}

// section 15.2.3.14 Object.keys(O)
inline JSVal ObjectKeys(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.keys", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject()) {
      JSObject* const obj = first.object();
      Context* const ctx = args.ctx();
      std::vector<Symbol> keys;
      obj->GetOwnPropertyNames(ctx, &keys, JSObject::kExcludeNotEnumerable);
      JSArray* const ary = JSArray::New(args.ctx(), keys.size());
      uint32_t index = 0;
      for (std::vector<Symbol>::const_iterator it = keys.begin(),
           last = keys.end(); it != last; ++it, ++index) {
        ary->DefineOwnPropertyWithIndex(
            ctx, index,
            DataDescriptor(
                args.ctx()->ToString(*it),
                PropertyDescriptor::WRITABLE |
                PropertyDescriptor::ENUMERABLE |
                PropertyDescriptor::CONFIGURABLE),
            false, IV_LV5_ERROR(e));
      }
      return ary;
    }
  }
  e->Report(Error::Type,
            "Object.keys requires Object argument");
  return JSUndefined;
}

// section 15.2.4.2 Object.prototype.toString()
inline JSVal ObjectToString(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.prototype.toString", args, e);
  const JSVal& this_binding = args.this_binding();
  if (this_binding.IsUndefined()) {
    return JSString::NewAsciiString(args.ctx(), "[object Undefined]");
  }
  if (this_binding.IsNull()) {
    return JSString::NewAsciiString(args.ctx(), "[object Null]");
  }
  JSObject* const obj = this_binding.ToObject(args.ctx(), IV_LV5_ERROR(e));
  StringBuilder builder;
  builder.Append("[object ");
  builder.Append(obj->cls()->name);
  builder.Append("]");
  return builder.Build(args.ctx());
}

// section 15.2.4.3 Object.prototype.toLocaleString()
inline JSVal ObjectToLocaleString(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.prototype.toLocaleString", args, e);
  JSObject* const obj =
      args.this_binding().ToObject(args.ctx(), IV_LV5_ERROR(e));
  Context* const ctx = args.ctx();
  const JSVal toString =
      obj->Get(ctx, symbol::toString, IV_LV5_ERROR(e));
  if (!toString.IsCallable()) {
    e->Report(Error::Type, "toString is not callable");
    return JSUndefined;
  }
  ScopedArguments arguments(ctx, 0, IV_LV5_ERROR(e));
  return toString.object()->AsCallable()->Call(&arguments, obj, e);
}

// section 15.2.4.4 Object.prototype.valueOf()
inline JSVal ObjectValueOf(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.prototype.valueOf", args, e);
  JSObject* const obj =
      args.this_binding().ToObject(args.ctx(), IV_LV5_ERROR(e));
  if (obj->IsNativeObject()) {
    return obj;
  } else {
    // 15.2.2.1 step 1.a.ii
    // 15.2.4.4 step 2.a
    // implementation dependent host object behavior
    return JSUndefined;
  }
}

// section 15.2.4.5 Object.prototype.hasOwnProperty(V)
inline JSVal ObjectHasOwnProperty(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.prototype.hasOwnProperty", args, e);
  if (args.size() > 0) {
    const JSVal& val = args[0];
    Context* const ctx = args.ctx();
    JSString* const str = val.ToString(ctx, IV_LV5_ERROR(e));
    JSObject* const obj =
        args.this_binding().ToObject(ctx, IV_LV5_ERROR(e));
    if (!obj->GetOwnProperty(ctx, context::Intern(ctx, str)).IsEmpty()) {
      return JSTrue;
    } else {
      return JSFalse;
    }
  } else {
    return JSFalse;
  }
}

// section 15.2.4.6 Object.prototype.isPrototypeOf(V)
inline JSVal ObjectIsPrototypeOf(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.prototype.isPrototypeOf", args, e);
  if (args.size() > 0) {
    const JSVal& first = args[0];
    if (first.IsObject()) {
      JSObject* const v = first.object();
      JSObject* const obj =
          args.this_binding().ToObject(args.ctx(), IV_LV5_ERROR(e));
      JSObject* proto = v->prototype();
      while (proto) {
        if (obj == proto) {
          return JSTrue;
        }
        proto = proto->prototype();
      }
    }
  }
  return JSFalse;
}

// section 15.2.4.7 Object.prototype.propertyIsEnumerable(V)
inline JSVal ObjectPropertyIsEnumerable(const Arguments& args, Error* e) {
  IV_LV5_CONSTRUCTOR_CHECK("Object.prototype.propertyIsEnumerable", args, e);
  Symbol name;
  if (args.size() > 0) {
    JSString* const str = args[0].ToString(args.ctx(), IV_LV5_ERROR(e));
    name = context::Intern(args.ctx(), str);
  } else {
    name = context::Intern(args.ctx(), "undefined");
  }
  JSObject* const obj =
      args.this_binding().ToObject(args.ctx(), IV_LV5_ERROR(e));
  const PropertyDescriptor desc = obj->GetOwnProperty(args.ctx(), name);
  if (desc.IsEmpty()) {
    return JSFalse;
  }
  return JSVal::Bool(desc.IsEnumerable());
}

} } }  // namespace iv::lv5::runtime
#endif  // _IV_LV5_RUNTIME_OBJECT_H_
