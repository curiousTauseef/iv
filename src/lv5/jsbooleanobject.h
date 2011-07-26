#ifndef _IV_LV5_JSBOOLEANOBJECT_H_
#define _IV_LV5_JSBOOLEANOBJECT_H_
#include "lv5/jsobject.h"
#include "lv5/context_utils.h"
namespace iv {
namespace lv5 {

class JSBooleanObject : public JSObject {
 public:
  explicit JSBooleanObject(bool value) : value_(value) { }
  bool value() const {
    return value_;
  }

  static const Class* GetClass() {
    static const Class cls = {
      "Boolean",
      Class::Boolean
    };
    return &cls;
  }

  static JSBooleanObject* New(Context* ctx, bool value) {
    JSBooleanObject* const obj = new JSBooleanObject(value);
    obj->set_cls(JSBooleanObject::GetClass());
    obj->set_prototype(context::GetClassSlot(ctx, Class::Boolean).prototype);
    return obj;
  }

  static JSBooleanObject* NewPlain(Context* ctx, bool value) {
    return new JSBooleanObject(value);
  }

 private:
  bool value_;
};

} }  // namespace iv::lv5
#endif  // _IV_LV5_JSBOOLEANOBJECT_H_