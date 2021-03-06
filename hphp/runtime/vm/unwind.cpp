/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-2013 Facebook, Inc. (http://www.facebook.com)     |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/
#include "hphp/runtime/vm/unwind.h"

#include <boost/implicit_cast.hpp>

#include "folly/ScopeGuard.h"

#include "hphp/util/trace.h"
#include "hphp/runtime/vm/core_types.h"
#include "hphp/runtime/vm/bytecode.h"
#include "hphp/runtime/vm/func.h"
#include "hphp/runtime/vm/unit.h"
#include "hphp/runtime/vm/runtime.h"
#include "hphp/runtime/vm/debugger_hook.h"

namespace HPHP {

TRACE_SET_MOD(unwind);
using boost::implicit_cast;

namespace {

//////////////////////////////////////////////////////////////////////
#if (defined(DEBUG) || defined(USE_TRACE))
std::string describeFault(const Fault& f) {
  switch (f.m_faultType) {
  case Fault::Type::UserException:
    return folly::format("[user exception] {}",
                         implicit_cast<void*>(f.m_userException)).str();
  case Fault::Type::CppException:
    return folly::format("[cpp exception] {}",
                         implicit_cast<void*>(f.m_cppException)).str();
  }
  not_reached();
}
#endif

void discardStackTemps(const ActRec* const fp,
                       Stack& stack,
                       Offset const bcOffset) {
  FTRACE(2, "discardStackTemps with fp {} sp {} pc {}\n",
         implicit_cast<const void*>(fp),
         implicit_cast<void*>(stack.top()),
         bcOffset);

  visitStackElems(
    fp, stack.top(), bcOffset,
    [&] (ActRec* ar) {
      assert(ar == reinterpret_cast<ActRec*>(stack.top()));
      if (ar->isFromFPushCtor()) {
        assert(ar->hasThis());
        ar->getThis()->setNoDestruct();
      }
      stack.popAR();
    },
    [&] (TypedValue* tv) {
      assert(tv == stack.top());
      stack.popTV();
    }
  );

  FTRACE(2, "discardStackTemps ends with sp = {}\n",
         implicit_cast<void*>(stack.top()));
}

UnwindAction checkHandlers(const EHEnt* eh,
                           const ActRec* const fp,
                           PC& pc,
                           Fault& fault) {
  auto const func = fp->m_func;

  FTRACE(1, "checkHandlers: func {} ({})\n",
         func->fullName()->data(),
         func->unit()->filepath()->data());

  /*
   * This code is repeatedly called with the same offset when an
   * exception is raised and rethrown by fault handlers.  The
   * `faultNest' iterator is here to skip the EHEnt handlers that have
   * already been run for this in-flight exception.
   */
  int faultNest = 0;
  for (;;) {
    assert(faultNest <= fault.m_handledCount);
    if (faultNest == fault.m_handledCount) {
      ++fault.m_handledCount;

      switch (eh->m_type) {
      case EHEnt::Type::Fault:
        FTRACE(1, "checkHandlers: entering fault at {}: save {}\n",
               eh->m_fault,
               func->unit()->offsetOf(pc));
        fault.m_savedRaiseOffset = func->unit()->offsetOf(pc);
        pc = func->unit()->entry() + eh->m_fault;
        DEBUGGER_ATTACHED_ONLY(phpDebuggerExceptionHandlerHook());
        return UnwindAction::ResumeVM;
      case EHEnt::Type::Catch:
        // Note: we skip catch clauses if we have a pending C++ exception
        // as part of our efforts to avoid running more PHP code in the
        // face of such exceptions.
        if (fault.m_faultType == Fault::Type::UserException &&
            ThreadInfo::s_threadInfo->m_pendingException == nullptr) {
          auto const obj = fault.m_userException;
          for (auto& idOff : eh->m_catches) {
            auto handler = func->unit()->at(idOff.second);
            FTRACE(1, "checkHandlers: catch candidate {}\n", handler);
            auto const cls = Unit::lookupClass(
              func->unit()->lookupNamedEntityId(idOff.first)
            );
            if (!cls || !obj->instanceof(cls)) continue;

            FTRACE(1, "checkHandlers: entering catch at {}\n", pc);
            pc = handler;
            DEBUGGER_ATTACHED_ONLY(phpDebuggerExceptionHandlerHook());
            return UnwindAction::ResumeVM;
          }
        }
        break;
      }
    }

    if (eh->m_parentIndex != -1) {
      eh = &func->ehtab()[eh->m_parentIndex];
    } else {
      break;
    }
    ++faultNest;
  }

  return UnwindAction::Propagate;
}

void tearDownFrame(ActRec*& fp, Stack& stack, PC& pc, Offset& faultOffset) {
  auto const func = fp->m_func;
  auto const curOp = *reinterpret_cast<const Opcode*>(pc);
  auto const unwindingGeneratorFrame = func->isGenerator();
  auto const unwindingReturningFrame = curOp == OpRetC || curOp == OpRetV;
  auto const prevFp = fp->arGetSfp();

  FTRACE(1, "tearDownFrame: {} ({})\n  fp {} prevFp {}\n",
         func->fullName()->data(),
         func->unit()->filepath()->data(),
         implicit_cast<void*>(fp),
         implicit_cast<void*>(prevFp));

  if (fp->isFromFPushCtor() && fp->hasThis()) {
    fp->getThis()->setNoDestruct();
  }

  // A generator's locals don't live on this stack.
  if (LIKELY(!unwindingGeneratorFrame)) {
    /*
     * If we're unwinding through a frame that's returning, it's only
     * possible that its locals have already been decref'd.
     *
     * Here's why:
     *
     *   - If a destructor for any of these things throws a php
     *     exception, it's swallowed at the dtor boundary and we keep
     *     running php.
     *
     *   - If the destructor for any of these things throws a fatal,
     *     it's swallowed, and we set surprise flags to throw a fatal
     *     from now on.
     *
     *   - If the second case happened and we have to run another
     *     destructor, its enter hook will throw, but it will be
     *     swallowed again.
     *
     *   - Finally, the exit hook for the returning function can
     *     throw, but this happens last so everything is destructed.
     *
     */
    if (!unwindingReturningFrame) {
      try {
        // Note that we must convert locals and the $this to
        // uninit/zero during unwind.  This is because a backtrace
        // from another destructing object during this unwind may try
        // to read them.
        frame_free_locals_unwind(fp, func->numLocals());
      } catch (...) {}
    }
    stack.ndiscard(func->numSlotsInFrame());
    stack.discardAR();
  }

  assert(stack.isValidAddress(reinterpret_cast<uintptr_t>(prevFp)) ||
         prevFp->m_func->isGenerator());
  auto const prevOff = fp->m_soff + prevFp->m_func->base();
  pc = prevFp->m_func->unit()->at(prevOff);
  fp = prevFp;
  faultOffset = prevOff;
}

/*
 * Unwinding proceeds as follows:
 *
 *   - Discard all evaluation stack temporaries (including pre-live
 *     activation records).
 *
 *   - Check if the faultOffset that raised the exception is inside a
 *     protected region, if so, if it can handle the Fault resume the
 *     VM at the handler.
 *
 *   - Failing any of the above, pop the frame for the current
 *     function.  If the current function was the last frame in the
 *     current VM nesting level, return UnwindAction::Propagate,
 *     otherwise go to the first step and repeat this process in the
 *     caller's frame.
 *
 * Note: it's important that the unwinder makes a copy of the Fault
 * it's currently operating on, as the underlying faults vector may
 * reallocate due to nested exception handling.
 */
UnwindAction unwind(ActRec*& fp,
                    Stack& stack,
                    PC& pc,
                    Offset faultOffset, // distinct from pc after iopUnwind
                    Fault fault) {
  FTRACE(1, "entering unwinder for fault: {}\n", describeFault(fault));
  SCOPE_EXIT {
    FTRACE(1, "leaving unwinder for fault: {}\n", describeFault(fault));
  };

  for (;;) {
    FTRACE(1, "unwind: func {}, faultOffset {} fp {}\n",
           fp->m_func->name()->data(),
           faultOffset,
           implicit_cast<void*>(fp));

    /*
     * If the handledCount is non-zero, we've already this fault once
     * while unwinding this frema, and popped all eval stack
     * temporaries the first time it was thrown (before entering a
     * fault funclet).  When the Unwind instruction was executed in
     * the funclet, the eval stack must have been left empty again.
     *
     * (We have to skip discardStackTemps in this case because it will
     * look for FPI regions and assume the stack offsets correspond to
     * what the FPI table expects.)
     */
    if (fault.m_handledCount == 0) {
      discardStackTemps(fp, stack, faultOffset);
    }

    if (const EHEnt* eh = fp->m_func->findEH(faultOffset)) {
      switch (checkHandlers(eh, fp, pc, fault)) {
      case UnwindAction::ResumeVM:
        // We've kept our own copy of the Fault, because m_faults may
        // change if we have a reentry during unwinding.  When we're
        // ready to resume, we need to replace the fault to reflect
        // any state changes we've made (handledCount, etc).
        g_vmContext->m_faults.back() = fault;
        return UnwindAction::ResumeVM;
      case UnwindAction::Propagate:
        break;
      }
    }

    // We found no more handlers in this frame, so the nested fault
    // count starts over for the caller frame.
    fault.m_handledCount = 0;

    auto const lastFrameForNesting = fp == fp->arGetSfp();
    tearDownFrame(fp, stack, pc, faultOffset);
    if (lastFrameForNesting) {
      FTRACE(1, "unwind: reached the end of this nesting's ActRec chain\n");
      break;
    }
  }

  return UnwindAction::Propagate;
}

const StaticString s_hphpd_break("hphpd_break");
const StaticString s_fb_enable_code_coverage("fb_enable_code_coverage");

// Unwind the frame for a builtin.  Currently only used when switching
// modes for hphpd_break and fb_enable_code_coverage.
void unwindBuiltinFrame() {
  auto& stack = g_vmContext->getStack();
  auto& fp = g_vmContext->m_fp;

  assert(fp->m_func->info());
  assert(fp->m_func->name()->isame(s_hphpd_break.get()) ||
         fp->m_func->name()->isame(s_fb_enable_code_coverage.get()));

  // Free any values that may be on the eval stack.  We know there
  // can't be FPI regions and it can't be a generator body because
  // it's a builtin frame.
  auto const evalTop = reinterpret_cast<TypedValue*>(g_vmContext->getFP());
  while (stack.topTV() < evalTop) {
    stack.popTV();
  }

  // Free the locals and VarEnv if there is one
  frame_free_locals_inl(fp, fp->m_func->numLocals());

  // Tear down the frame
  Offset pc = -1;
  ActRec* sfp = g_vmContext->getPrevVMState(fp, &pc);
  assert(pc != -1);
  fp = sfp;
  g_vmContext->m_pc = fp->m_func->unit()->at(pc);
  stack.discardAR();
}

void pushFault(Exception* e) {
  Fault f;
  f.m_faultType = Fault::Type::CppException;
  f.m_cppException = e;
  g_vmContext->m_faults.push_back(f);
  FTRACE(1, "pushing new fault: {}\n", describeFault(f));
}

void pushFault(const Object& o) {
  Fault f;
  f.m_faultType = Fault::Type::UserException;
  f.m_userException = o.get();
  f.m_userException->incRefCount();
  g_vmContext->m_faults.push_back(f);
  FTRACE(1, "pushing new fault: {}\n", describeFault(f));
}

UnwindAction enterUnwinder() {
  auto fault = g_vmContext->m_faults.back();
  return unwind(
    g_vmContext->m_fp,      // by ref
    g_vmContext->getStack(),// by ref
    g_vmContext->m_pc,      // by ref
    g_vmContext->pcOff(),
    fault
  );
}

//////////////////////////////////////////////////////////////////////

}

UnwindAction exception_handler() noexcept {
  FTRACE(1, "unwind exception_handler\n");

  g_vmContext->checkRegState();

  try { throw; }

  /*
   * Unwind (repropagating from a fault funclet) is slightly different
   * from the throw cases, because we need to re-raise the exception
   * as if it came from the same offset to handle nested fault
   * handlers correctly, and we continue propagating the current Fault
   * instead of pushing a new one.
   */
  catch (const VMPrepareUnwind&) {
    Fault fault = g_vmContext->m_faults.back();
    Offset faultOffset = fault.m_savedRaiseOffset;
    FTRACE(1, "unwind: restoring offset {}\n", faultOffset);
    assert(faultOffset != kInvalidOffset);
    fault.m_savedRaiseOffset = kInvalidOffset;
    return unwind(
      g_vmContext->m_fp,
      g_vmContext->getStack(),
      g_vmContext->m_pc,
      faultOffset,
      fault
    );
  }

  catch (const Object& o) {
    pushFault(o);
    return enterUnwinder();
  }

  catch (VMSwitchMode&) {
    return UnwindAction::ResumeVM;
  }

  catch (VMSwitchModeBuiltin&) {
    unwindBuiltinFrame();
    g_vmContext->getStack().pushNull(); // return value
    return UnwindAction::ResumeVM;
  }

  catch (Exception& e) {
    pushFault(e.clone());;
    return enterUnwinder();
  }

  catch (std::exception& e) {
    pushFault(new Exception("unexpected %s: %s", typeid(e).name(), e.what()));
    return enterUnwinder();
  }

  catch (...) {
    pushFault(new Exception("unknown exception"));
    return enterUnwinder();
  }

  not_reached();
}

//////////////////////////////////////////////////////////////////////

}
