// railgun vm stack
// construct Frame on this stack,
// and traverse Frames when GC maker comes
#ifndef IV_LV5_RAILGUN_STACK_H_
#define IV_LV5_RAILGUN_STACK_H_
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <iterator>
#include <new>
#include <iv/noncopyable.h>
#include <iv/singleton.h>
#include <iv/os_allocator.h>
#include <iv/lv5/internal.h>
#include <iv/lv5/jsval.h>
#include <iv/lv5/gc_kind.h>
#include <iv/lv5/stack.h>
#include <iv/lv5/railgun/frame.h>
#include <iv/lv5/railgun/direct_threading.h>
#include <iv/lv5/radio/core_fwd.h>
namespace iv {
namespace lv5 {
namespace railgun {

class Stack : public lv5::Stack {
 public:
  class Resource : public GCKind<Resource> {
   public:
    explicit Resource(Stack* stack)
      : stack_(stack) {
    }

    Stack* stack() const {
      return stack_;
    }

    GC_ms_entry* MarkChildren(GC_word* top,
                              GC_ms_entry* entry,
                              GC_ms_entry* mark_sp_limit,
                              GC_word env) {
      if (stack_) {
        Frame* current = stack_->current_;
        if (current) {
          // mark Frame member
          entry = MarkFrame(entry, mark_sp_limit,
                            current, stack_->stack_pointer_);
          // traverse frames
          for (Frame *next = current, *now = current->prev_;
               now; next = now, now = next->prev_) {
            entry = MarkFrame(entry, mark_sp_limit, now, next->GetFrameBase());
          }
        }
      }
      return entry;
    }

   private:
    Stack* stack_;
  };

  explicit Stack(JSVal global)
    : lv5::Stack(global),
      resource_(NULL),
      base_(NULL),
      current_(NULL) {
    resource_ = new Resource(this);
  }

  explicit Stack(DispatchTableTag tag)
    : lv5::Stack(lv5::Stack::EmptyTag()) { }  // empty

  ~Stack() {
    if (stack_) {
      delete resource_;
    }
  }

  // returns new frame for function call
  Frame* NewCodeFrame(Context* ctx,
                      JSVal* sp,
                      Code* code,
                      JSEnv* env,
                      JSVal callee,
                      Instruction* pc,
                      std::size_t argc,
                      bool constructor_call) {
    assert(code);
    if (JSVal* mem = GainFrame(sp, code)) {
      Frame* frame = reinterpret_cast<Frame*>(mem);
      frame->code_ = code;
      frame->prev_pc_ = pc;
      frame->variable_env_ = frame->lexical_env_ = env;
      frame->prev_ = current_;
      frame->ret_ = JSUndefined;
      frame->callee_ = callee;
      frame->argc_ = argc;
      frame->dynamic_env_level_ = 0;
      frame->localc_ = code->stack_size();
      std::fill_n<JSVal*, std::size_t, JSVal>(
          frame->GetLocal(), frame->localc_, JSUndefined);
      frame->constructor_call_ = constructor_call;
      current_ = frame;
      return frame;
    } else {
      // stack overflow
      return NULL;
    }
  }

  Frame* NewEvalFrame(Context* ctx,
                      JSVal* sp,
                      Code* code,
                      JSEnv* variable_env,
                      JSEnv* lexical_env) {
    assert(code);
    if (JSVal* mem = GainFrame(sp, code)) {
      Frame* frame = reinterpret_cast<Frame*>(mem);
      frame->code_ = code;
      frame->prev_pc_ = NULL;
      frame->variable_env_ = variable_env;
      frame->lexical_env_ = lexical_env;
      frame->prev_ = current_;
      frame->ret_ = JSUndefined;
      frame->callee_ = JSUndefined;
      frame->argc_ = 0;
      frame->dynamic_env_level_ = 0;
      frame->localc_ = 0;
      frame->constructor_call_ = false;
      current_ = frame;
      return frame;
    } else {
      // stack overflow
      return NULL;
    }
  }

  Frame* NewGlobalFrame(Context* ctx, Code* code) {
    assert(code);
    if (JSVal* mem = GainFrame(stack_ + 1, code)) {
      Frame* frame = reinterpret_cast<Frame*>(mem);
      frame->code_ = code;
      frame->prev_pc_ = NULL;
      frame->variable_env_ = frame->lexical_env_ = ctx->global_env();
      frame->prev_ = NULL;
      frame->ret_ = JSUndefined;
      frame->callee_ = JSUndefined;
      frame->argc_ = 0;
      frame->dynamic_env_level_ = 0;
      frame->localc_ = 0;
      frame->constructor_call_ = false;
      current_ = frame;
      return frame;
    } else {
      // stack overflow
      return NULL;
    }
  }

  Frame* Unwind(Frame* frame) {
    assert(current_ == frame);
    assert(frame->code());
    SetSafeStackPointerForFrame(frame, frame->prev_);
    current_ = frame->prev_;
    return current_;
  }

  Frame* current() {
    return current_;
  }

  void MarkChildren(radio::Core* core) {
    // mark Frame member
    MarkFrame(core, current_, stack_pointer_);
    // traverse frames
    for (Frame *next = current_, *now = current_->prev_;
         now; next = now, now = next->prev_) {
      MarkFrame(core, now, next->GetFrameBase());
    }
  }

 private:
  static GC_ms_entry* MarkFrame(GC_ms_entry* entry,
                                GC_ms_entry* mark_sp_limit,
                                Frame* frame, JSVal* last) {
    entry = GC_MARK_AND_PUSH(frame->code_,
                             entry, mark_sp_limit,
                             reinterpret_cast<void**>(&frame));
    entry = GC_MARK_AND_PUSH(frame->lexical_env_,
                             entry, mark_sp_limit,
                             reinterpret_cast<void**>(&frame));
    entry = GC_MARK_AND_PUSH(frame->variable_env_,
                             entry, mark_sp_limit,
                             reinterpret_cast<void**>(&frame));
    if (frame->ret_.IsCell()) {
      radio::Cell* ptr = frame->ret_.cell();
      entry = GC_MARK_AND_PUSH(ptr,
                               entry, mark_sp_limit,
                               reinterpret_cast<void**>(&frame));
    }
    if (frame->callee_.IsCell()) {
      radio::Cell* ptr = frame->callee_.cell();
      entry = GC_MARK_AND_PUSH(ptr,
                               entry, mark_sp_limit,
                               reinterpret_cast<void**>(&frame));
    }

    // start current frame marking
    for (JSVal *it = frame->GetLocal(); it != last; ++it) {
      if (it->IsCell()) {
        radio::Cell* ptr = it->cell();
        entry = GC_MARK_AND_PUSH(ptr,
                                 entry, mark_sp_limit,
                                 reinterpret_cast<void**>(&frame));
      }
    }
    return entry;
  }

  void MarkFrame(radio::Core* core, Frame* frame, JSVal* last) {
    core->MarkCell(frame->code_);
    core->MarkCell(frame->lexical_env_);
    core->MarkCell(frame->variable_env_);
    core->MarkValue(frame->ret_);
    core->MarkValue(frame->callee_);
    std::for_each(frame->GetLocal(), last, radio::Core::Marker(core));
  }

  void SetSafeStackPointerForFrame(Frame* prev, Frame* current) {
    if (current) {
      JSVal* frame_last = current->GetFrameEnd();
      JSVal* prev_first = prev->GetFrameBase();
      stack_pointer_ = std::max(frame_last, prev_first);
    } else {
      // previous of Global Frame is NULL
      stack_pointer_ = stack_ + 1;
    }
  }

  JSVal* GainFrame(JSVal* top, Code* code) {
    assert(stack_ < top);
    assert(top <= stack_pointer_);
    stack_pointer_ = top;
    return Gain(Frame::GetFrameSize(code->stack_depth()));
  }

  Resource* resource_;
  Frame* base_;
  Frame* current_;
};

} } }  // namespace iv::lv5::railgun
#endif  // IV_LV5_RAILGUN_STACK_H_
