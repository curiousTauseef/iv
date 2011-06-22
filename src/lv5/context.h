#ifndef _IV_LV5_CONTEXT_H_
#define _IV_LV5_CONTEXT_H_
#include <cstddef>
#include "stringpiece.h"
#include "ustringpiece.h"
#include "noncopyable.h"
#include "lv5/error_check.h"
#include "lv5/jsval.h"
#include "lv5/jsenv.h"
#include "lv5/jsobject.h"
#include "lv5/jsfunction.h"
#include "lv5/class.h"
#include "lv5/error.h"
#include "lv5/specialized_ast.h"
#include "lv5/global_data.h"
#include "lv5/context_utils.h"

namespace iv {
namespace lv5 {
namespace runtime {

JSVal ThrowTypeError(const Arguments& args, Error* error);

}  // namespace runtime
namespace bind {
class Object;
}  // namespace bind

class SymbolChecker;
class JSEnv;

class Context : private core::Noncopyable<> {
 public:
  friend class SymbolChecker;
  friend const core::UString& context::GetSymbolString(const Context* ctx,
                                                       const Symbol& sym);
  friend const Class& context::Cls(Context* ctx, const Symbol& name);
  friend const Class& context::Cls(Context* ctx,
                                   const core::StringPiece& str);
  friend Symbol context::Intern(Context* ctx, const core::StringPiece& str);
  friend Symbol context::Intern(Context* ctx, const core::UStringPiece& str);
  friend Symbol context::Intern(Context* ctx, uint32_t index);
  friend Symbol context::Intern(Context* ctx, double number);

  friend void RegisterLiteralRegExp(Context* ctx, JSRegExpImpl* reg);

  Context();

  const JSObject* global_obj() const {
    return global_data_.global_obj();
  }

  JSObject* global_obj() {
    return global_data_.global_obj();
  }

  JSEnv* lexical_env() const {
    return lexical_env_;
  }

  void set_lexical_env(JSEnv* env) {
    lexical_env_ = env;
  }

  JSEnv* variable_env() const {
    return variable_env_;
  }

  void set_variable_env(JSEnv* env) {
    variable_env_ = env;
  }

  JSEnv* global_env() const {
    return global_env_;
  }

  virtual JSVal* StackGain(std::size_t size) { return NULL; }
  virtual void StackRelease(std::size_t size) { }

  template<typename Func>
  void DefineFunction(const Func& f,
                      const core::StringPiece& func_name,
                      std::size_t n) {
    Error e;
    JSFunction* const func = JSNativeFunction::New(this, f, n);
    const Symbol name = context::Intern(this, func_name);
    variable_env_->CreateMutableBinding(this, name, false, IV_LV5_ERROR_VOID(&e));
    variable_env_->SetMutableBinding(this,
                                     name,
                                     func, false, &e);
  }

  template<JSVal (*func)(const Arguments&, Error*), std::size_t n>
  void DefineFunction(const core::StringPiece& func_name) {
    Error e;
    JSFunction* const f = JSInlinedFunction<func, n>::New(this);
    const Symbol name = context::Intern(this, func_name);
    variable_env_->CreateMutableBinding(this, name, false, IV_LV5_ERROR_VOID(&e));
    variable_env_->SetMutableBinding(this, name,
                                     f, false, &e);
  }

  template<JSAPI FunctionConstructor, JSAPI GlobalEval>
  void Initialize() {
    InitContext(JSInlinedFunction<FunctionConstructor, 1>::NewPlain(this),
                JSInlinedFunction<GlobalEval, 1>::NewPlain(this));
  }

  JSFunction* throw_type_error() {
    return &throw_type_error_;
  }

  bool IsArray(const JSObject& obj) {
    return obj.class_name() == global_data_.Array_symbol();
  }

  double Random();

  GlobalData* global_data() {
    return &global_data_;
  }

  const GlobalData* global_data() const {
    return &global_data_;
  }

  JSString* ToString(Symbol sym);

 private:
  void InitContext(JSFunction* func_constructor, JSFunction* eval_function);

  void InitGlobal(const Class& func_cls,
                  JSObject* obj_proto, JSFunction* eval_function,
                  bind::Object* global_binder);

  void InitArray(const Class& func_cls,
                 JSObject* obj_proto, bind::Object* global_binder);

  void InitString(const Class& func_cls,
                 JSObject* obj_proto, bind::Object* global_binder);

  void InitBoolean(const Class& func_cls,
                   JSObject* obj_proto, bind::Object* global_binder);

  void InitNumber(const Class& func_cls,
                  JSObject* obj_proto, bind::Object* global_binder);

  void InitMath(const Class& func_cls,
                JSObject* obj_proto, bind::Object* global_binder);

  void InitDate(const Class& func_cls,
                JSObject* obj_proto, bind::Object* global_binder);

  void InitRegExp(const Class& func_cls,
                 JSObject* obj_proto, bind::Object* global_binder);

  void InitError(const Class& func_cls,
                 JSObject* obj_proto, bind::Object* global_binder);

  void InitJSON(const Class& func_cls,
                JSObject* obj_proto, bind::Object* global_binder);

  GlobalData global_data_;
  JSInlinedFunction<&runtime::ThrowTypeError, 0> throw_type_error_;
  JSEnv* lexical_env_;
  JSEnv* variable_env_;
  JSEnv* global_env_;
};

} }  // namespace iv::lv5
#endif  // _IV_LV5_CONTEXT_H_
