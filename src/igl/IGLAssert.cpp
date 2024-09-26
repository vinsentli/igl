/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#define IGL_COMMON_SKIP_CHECK
#include <cstdio>
#include <igl/IGLAssert.h>
#include <igl/Log.h>

// ----------------------------------------------------------------------------

namespace igl {

// Toggle debug break on/off at runtime
#if IGL_DEBUG
static bool debugBreakEnabled = true;
#else
static bool debugBreakEnabled = false;
#endif // IGL_DEBUG

bool isDebugBreakEnabled() {
  return debugBreakEnabled;
}

void setDebugBreakEnabled(bool enabled) {
  debugBreakEnabled = enabled;
}

} // namespace igl

#if IGL_PLATFORM_APPLE || IGL_PLATFORM_ANDROID || IGL_PLATFORM_LINUX
#define IGL_DEBUGGER_SIGTRAP 1
#include <signal.h>
#elif IGL_PLATFORM_WIN
#include <windows.h>
#endif

void _IGLDebugBreak() {
  if (igl::isDebugBreakEnabled()) {
#ifdef IGL_DEBUGGER_SIGTRAP
    raise(SIGTRAP);
#elif IGL_PLATFORM_WIN
    if (!IsDebuggerPresent()) {
      IGLLog(IGLLogLevel::LOG_ERROR, "[IGL] Skipping debug break - debugger not present");
      return;
    }
    __debugbreak();
#else
#warning "IGLDebugBreak() not implemented on this platform"
#endif
  }
}

// ----------------------------------------------------------------------------

#if IGL_REPORT_ERROR_ENABLED

// Default handler is no-op.
// If there's an error, IGL_VERIFY will trap in dev builds
static void _IGLReportErrorDefault(const char* file,
                                   const char* func,
                                   int line,
                                   const char* category,
                                   const char* format,
                                   ...) {}

static IGLReportErrorFunc& GetErrorHandler() {
  static IGLReportErrorFunc sHandler = _IGLReportErrorDefault;
  return sHandler;
}

IGL_API void IGLReportErrorSetHandler(IGLReportErrorFunc handler) {
  if (!handler) {
    handler = _IGLReportErrorDefault; // prevent null handler
  }
  GetErrorHandler() = handler;
}

IGL_API IGLReportErrorFunc IGLReportErrorGetHandler(void) {
  return GetErrorHandler();
}

#endif // IGL_REPORT_ERROR_ENABLED
