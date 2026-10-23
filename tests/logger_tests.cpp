#include "Logger.h"
#include "dag_assert.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

// Issue #12: before the log handler exists, an assert or a log call must not touch it. Its accessor asserts,
// and an assert logs, so a call that touches it recursed until the stack overflowed.

static int failures = 0;

static void check(bool value, const char *what) {
  if (!value) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

static size_t count(const std::string &text, const std::string &part) {
  size_t n = 0;
  for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
    n++;
  return n;
}

int main() {
  check(!g_log_handler.init_state(), "no log handler before initialize()");

  std::ostringstream err;
  std::streambuf *old = std::cerr.rdbuf(err.rdbuf());
  LOGI("info before init");
  LOGE("error before init {}", 12);
  bool thrown = false;
  try {
    G_CHECK(1 + 1 == 3);
  } catch (const AssertException &) {
    thrown = true;
  }
  std::cerr.rdbuf(old);

  const std::string text = err.str();
  check(text.find("info before init") == std::string::npos, "info is dropped before init");
  check(text.find("error before init 12") != std::string::npos, "an error before init goes to stderr");
  check(thrown, "a failed check before init throws AssertException");
  check(count(text, "ASSERTION FAILED") == 1, "the failed check logs one time");
  check(!g_log_handler.init_state(), "logging does not create the handler");

  g_log_handler.initialize();
  g_log_handler->start_thread();
  check(g_log_handler.init_state(), "the handler exists after initialize()");
  check(log_level_allowed(DEFAULT_SINK_HANDLER, LOGLEVEL::INFO), "info passes after init");
  LOGE("error after init");
  g_log_handler->wait_until_empty();

  std::printf("logger_tests: %d failures\n", failures);
  return failures ? 1 : 0;
}
