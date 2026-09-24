/**
 * @file test_log.cpp
 *
 * Unit tests for the diagnostic log.
 *
 * What is worth testing here is not that a message can be printed - that is
 * visible the first time anyone runs the demo - but the three things that
 * fail silently: that the library is quiet when nobody asked it to speak,
 * that a level actually filters rather than merely being recorded, and that
 * a message longer than the stack buffer survives intact. A logger that
 * truncates a Vulkan validation error at 1024 bytes loses exactly the part
 * that says which object was wrong.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/cjelly/cj_log.h>
#include <ghoti.io/cjelly/log_internal.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
// The library reads its variables with getenv(), and on Windows only the C
// runtime's own setter changes what getenv() sees - SetEnvironmentVariable
// updates the process block, which the runtime copied at startup.  Setting a
// variable to the empty string is how _putenv_s removes one.
static int setenv(const char * name, const char * value, int) {
  return _putenv_s(name, value);
}
static int unsetenv(const char * name) {
  return _putenv_s(name, "");
}
#endif

namespace {

/** Collects what the library hands the sink, so a test can assert on the
 *  message the sink was given rather than on anything a stream did to it. */
struct Recorder {
  std::vector<std::pair<cj_log_level_t, std::string>> records;

  static void sink(cj_log_level_t level, const char * message, void * user) {
    static_cast<Recorder *>(user)->records.emplace_back(level, message);
  }
};

/** Installs a recorder and puts the level and sink back afterwards, so that
 *  one test cannot decide another test's starting state. */
class LogFixture : public ::testing::Test {
protected:
  Recorder rec;

  void SetUp() override {
    cj_log_set_sink(&Recorder::sink, &rec);
  }
  void TearDown() override {
    cj_log_set_sink(nullptr, nullptr);
    cj_log_set_level(CJ_LOG_ERROR);
  }
};

} // namespace

// --- What the library says when nobody has configured it -------------------

TEST(LogDefault, QuietUnlessSomethingFailed) {
  // Nothing has set a level and neither variable is set, so this is the
  // level a program that never mentions logging gets.
  unsetenv("CJELLY_LOG");
  unsetenv("CJELLY_DEBUG");
  cj_log__reset_for_test();
  EXPECT_EQ(cj_log_get_level(), CJ_LOG_ERROR);
}

TEST(LogDefault, EnvironmentIsActuallyConsulted) {
  setenv("CJELLY_LOG", "debug", 1);
  cj_log__reset_for_test();
  EXPECT_EQ(cj_log_get_level(), CJ_LOG_DEBUG);
  unsetenv("CJELLY_LOG");
  cj_log__reset_for_test();
}

TEST(LogDefault, CjellyDebugStillMeansDebug) {
  // The spelling 21 call sites in cjelly.c used before there was a level.
  unsetenv("CJELLY_LOG");
  setenv("CJELLY_DEBUG", "1", 1);
  cj_log__reset_for_test();
  EXPECT_EQ(cj_log_get_level(), CJ_LOG_DEBUG);
  unsetenv("CJELLY_DEBUG");
  cj_log__reset_for_test();
}

TEST(LogDefault, ExplicitLevelBeatsTheEnvironment) {
  setenv("CJELLY_LOG", "trace", 1);
  cj_log__reset_for_test();
  cj_log_set_level(CJ_LOG_OFF);
  EXPECT_EQ(cj_log_get_level(), CJ_LOG_OFF);
  unsetenv("CJELLY_LOG");
  cj_log_set_level(CJ_LOG_ERROR);
  cj_log__reset_for_test();
}

// --- Parsing a level name --------------------------------------------------

TEST(LogParse, AcceptsEveryName) {
  const struct { const char * text; cj_log_level_t level; } cases[] = {
    {"off", CJ_LOG_OFF},       {"error", CJ_LOG_ERROR},
    {"warn", CJ_LOG_WARN},     {"warning", CJ_LOG_WARN},
    {"info", CJ_LOG_INFO},     {"debug", CJ_LOG_DEBUG},
    {"trace", CJ_LOG_TRACE},
    {"0", CJ_LOG_OFF},         {"5", CJ_LOG_TRACE},
  };
  for (const auto & c : cases) {
    cj_log_level_t got = CJ_LOG_OFF;
    EXPECT_TRUE(cj_log__parse_level(c.text, &got)) << c.text;
    EXPECT_EQ(got, c.level) << c.text;
  }
}

TEST(LogParse, RefusesWhatIsNotALevel) {
  cj_log_level_t got = CJ_LOG_INFO;
  for (const char * text : {"", "verbose", "DEBUG", "6", "-1", "12", "3x"}) {
    EXPECT_FALSE(cj_log__parse_level(text, &got)) << text;
  }
  EXPECT_FALSE(cj_log__parse_level(nullptr, &got));
  // A refusal must leave the caller's level alone, or a bad CJELLY_LOG would
  // silently change the level it failed to parse.
  EXPECT_EQ(got, CJ_LOG_INFO);
}

// --- Filtering -------------------------------------------------------------

TEST_F(LogFixture, LevelAdmitsItselfAndEverythingBelow) {
  cj_log_set_level(CJ_LOG_INFO);
  CJ_ERRORF("an error");
  CJ_WARNF("a warning");
  CJ_INFOF("some info");
  CJ_DEBUGF("a debug line");
  CJ_TRACEF("a trace line");

  ASSERT_EQ(rec.records.size(), 3u);
  EXPECT_EQ(rec.records[0].first, CJ_LOG_ERROR);
  EXPECT_EQ(rec.records[1].first, CJ_LOG_WARN);
  EXPECT_EQ(rec.records[2].first, CJ_LOG_INFO);
}

TEST_F(LogFixture, OffMeansOff) {
  cj_log_set_level(CJ_LOG_OFF);
  CJ_ERRORF("an error");
  EXPECT_TRUE(rec.records.empty());
}

TEST_F(LogFixture, FilteredMessageDoesNotEvaluateItsArguments) {
  // The macro tests the level before the call, so a filtered message must
  // not pay for formatting its operands - and must not run their side
  // effects either, which is the part that would bite.
  cj_log_set_level(CJ_LOG_ERROR);
  int calls = 0;
  auto count = [&calls]() { return ++calls; };
  CJ_DEBUGF("%d", count());
  EXPECT_EQ(calls, 0);
  CJ_ERRORF("%d", count());
  EXPECT_EQ(calls, 1);
}

TEST_F(LogFixture, WriteFiltersEvenWhenCalledDirectly) {
  // Not everything goes through the macro; the function is a gate too.
  cj_log_set_level(CJ_LOG_ERROR);
  cj_log_write(CJ_LOG_DEBUG, "should not appear");
  EXPECT_TRUE(rec.records.empty());
}

TEST_F(LogFixture, EmitIgnoresTheLevel) {
  // What the frame profiler relies on: the caller turned it on by name, so a
  // log level set for an unrelated reason must not cancel it.
  cj_log_set_level(CJ_LOG_OFF);
  cj_log_emit(CJ_LOG_INFO, "asked for");
  ASSERT_EQ(rec.records.size(), 1u);
  EXPECT_EQ(rec.records[0].second, "asked for");
}

// --- The message itself ----------------------------------------------------

TEST_F(LogFixture, MessageCarriesNoTrailingNewline) {
  // The sink decides its own framing, so a message that arrived with a "\n"
  // would double-space every destination that adds one.
  cj_log_set_level(CJ_LOG_ERROR);
  CJ_ERRORF("plain");
  ASSERT_EQ(rec.records.size(), 1u);
  EXPECT_EQ(rec.records[0].second, "plain");
}

TEST_F(LogFixture, LongMessageIsNotTruncated) {
  // A Vulkan validation error runs well past the stack buffer, and the end
  // of it is the half that names the object.
  cj_log_set_level(CJ_LOG_ERROR);
  std::string big(5000, 'x');
  big += "THE-IMPORTANT-PART";
  CJ_ERRORF("%s", big.c_str());
  ASSERT_EQ(rec.records.size(), 1u);
  EXPECT_EQ(rec.records[0].second.size(), big.size());
  EXPECT_EQ(rec.records[0].second, big);
}

TEST_F(LogFixture, MessageExactlyAtTheBufferBoundary) {
  cj_log_set_level(CJ_LOG_ERROR);
  for (size_t n : {size_t(1022), size_t(1023), size_t(1024), size_t(1025)}) {
    rec.records.clear();
    std::string s(n, 'y');
    CJ_ERRORF("%s", s.c_str());
    ASSERT_EQ(rec.records.size(), 1u) << n;
    EXPECT_EQ(rec.records[0].second, s) << n;
  }
}

// --- Names -----------------------------------------------------------------

TEST(LogLevelName, EveryLevelHasOne) {
  EXPECT_STREQ(cj_log_level_str(CJ_LOG_OFF), "OFF");
  EXPECT_STREQ(cj_log_level_str(CJ_LOG_ERROR), "ERROR");
  EXPECT_STREQ(cj_log_level_str(CJ_LOG_WARN), "WARN");
  EXPECT_STREQ(cj_log_level_str(CJ_LOG_INFO), "INFO");
  EXPECT_STREQ(cj_log_level_str(CJ_LOG_DEBUG), "DEBUG");
  EXPECT_STREQ(cj_log_level_str(CJ_LOG_TRACE), "TRACE");
  EXPECT_STREQ(cj_log_level_str((cj_log_level_t)99), "?");
}

// --- Vulkan validation severities ------------------------------------------

TEST(LogVkSeverity, EachSeverityGetsItsLevel) {
  EXPECT_EQ(cj_log__level_for_vk_severity(
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT),
      CJ_LOG_ERROR);
  EXPECT_EQ(cj_log__level_for_vk_severity(
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT),
      CJ_LOG_WARN);
  EXPECT_EQ(cj_log__level_for_vk_severity(
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT),
      CJ_LOG_DEBUG);
  EXPECT_EQ(cj_log__level_for_vk_severity(
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT),
      CJ_LOG_DEBUG);
}

TEST(LogVkSeverity, SeverityIsABitmaskAndTheWorstBitWins) {
  // A layer may set more than one bit.  Testing the gentler bit first would
  // demote a validation error to a warning, which at the default level is
  // the difference between seeing it and not.
  auto both = (VkDebugUtilsMessageSeverityFlagBitsEXT)(
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT);
  EXPECT_EQ(cj_log__level_for_vk_severity(both), CJ_LOG_ERROR);

  auto warn_and_info = (VkDebugUtilsMessageSeverityFlagBitsEXT)(
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT);
  EXPECT_EQ(cj_log__level_for_vk_severity(warn_and_info), CJ_LOG_WARN);
}

TEST(LogVkSeverity, NoSeverityBitIsNotAnError) {
  // Nothing should send this, and if something does, inventing an error is
  // worse than filing it with the chatter.
  EXPECT_EQ(cj_log__level_for_vk_severity(
                (VkDebugUtilsMessageSeverityFlagBitsEXT)0),
      CJ_LOG_DEBUG);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
