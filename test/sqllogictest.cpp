#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "argparse/argparse.hpp"
#include "common/constants.h"
#include "common/exceptions.h"
#include "common/result_writer.h"
#include "common/string_util.h"
#include "database/connection.h"
#include "database/database_engine.h"
#include "sqllogicparser.h"
// B+ 树验证器：仅依赖标准库，独立于 huadb 主库
#include "btree/parser.h"
#include "btree/verifier.h"

static constexpr const char *TEST_DIRECTORY = "huadb_test";
static constexpr int DEFAULT_TIMEOUT_SECONDS = 30;

static std::vector<std::string> labs_disable_verbose_feedback = {"lab6"};

static bool IsVerboseDisabled(const fs::path &path) {
  auto path_str = path.string();
  if (path_str.find("[hidden]") == std::string::npos) {
    return false;
  }
  for (const auto &lab : labs_disable_verbose_feedback) {
    if (path_str.find(lab) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::unordered_map<std::string, std::unique_ptr<huadb::Connection>> connections;

static size_t CountOccurrences(const std::string &haystack, const std::string &needle) {
  if (needle.empty()) {
    return 0;
  }
  size_t count = 0;
  size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

static std::string TrimSpaces(const std::string &s) {
  size_t begin = s.find_first_not_of(" \t\r");
  if (begin == std::string::npos) {
    return "";
  }
  size_t end = s.find_last_not_of(" \t\r");
  return s.substr(begin, end - begin + 1);
}

bool CompareResultByKeywords(const std::string &result, const std::string &expected_result,
                             std::ostringstream &error_stream) {
  // 每行形如 "<keyword> <spec>"，spec 为：
  //   整数 N -> 子串出现次数必须恰好为 N
  //   +     -> 子串至少出现一次
  //   -     -> 子串不出现（== 0）
  // 空行与以 '#' 开头的行忽略。
  auto expected_lines = huadb::StringUtil::Split(expected_result, '\n');
  bool all_ok = true;
  std::ostringstream diag;
  for (const auto &raw_line : expected_lines) {
    auto line = TrimSpaces(raw_line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    auto sep = line.find_last_of(" \t");
    if (sep == std::string::npos) {
      diag << "Malformed keyword assertion: \"" << line << "\" (expected `<keyword> <spec>`)" << std::endl;
      all_ok = false;
      continue;
    }
    std::string keyword = TrimSpaces(line.substr(0, sep));
    std::string spec = TrimSpaces(line.substr(sep + 1));
    if (keyword.empty() || spec.empty()) {
      diag << "Malformed keyword assertion: \"" << line << "\"" << std::endl;
      all_ok = false;
      continue;
    }
    size_t actual_count = CountOccurrences(result, keyword);
    bool ok = false;
    std::string spec_desc;
    if (spec == "+") {
      ok = actual_count >= 1;
      spec_desc = ">= 1";
    } else if (spec == "-") {
      ok = actual_count == 0;
      spec_desc = "== 0";
    } else {
      bool numeric = !spec.empty() && std::all_of(spec.begin(), spec.end(), [](char c) { return std::isdigit(c); });
      if (!numeric) {
        diag << "Invalid spec \"" << spec << "\" for keyword \"" << keyword << "\" (expected integer, '+' or '-')"
             << std::endl;
        all_ok = false;
        continue;
      }
      size_t expected_count = std::stoul(spec);
      ok = actual_count == expected_count;
      spec_desc = "== " + std::to_string(expected_count);
    }
    if (!ok) {
      diag << "Keyword \"" << keyword << "\": expected " << spec_desc << ", got " << actual_count << std::endl;
      all_ok = false;
    }
  }
  if (!all_ok) {
    error_stream << "Your Result:" << std::endl;
    error_stream << result << std::endl;
    error_stream << "Keyword check failures:" << std::endl;
    error_stream << diag.str();
    error_stream << "Expected (keyword assertions):" << std::endl;
    error_stream << expected_result;
  }
  return all_ok;
}

bool CompareResultByCount(const std::string &result, const std::string &expected_result,
                          std::ostringstream &error_stream) {
  // 期望 spec 为单行： 非负整数 N (== N) | '+' (>= 1) | '-' (== 0)
  std::string spec;
  {
    auto spec_lines = huadb::StringUtil::Split(expected_result, '\n');
    int non_empty = 0;
    for (const auto &raw : spec_lines) {
      auto t = TrimSpaces(raw);
      if (t.empty()) {
        continue;
      }
      ++non_empty;
      spec = t;
    }
    if (non_empty != 1) {
      error_stream << "Malformed count spec: expected exactly one line of `<N>|+|-`, got " << non_empty << " line(s)"
                   << std::endl;
      error_stream << "Expected:" << std::endl << expected_result;
      return false;
    }
  }

  size_t actual = 0;
  for (const auto &line : huadb::StringUtil::Split(result, '\n')) {
    if (!TrimSpaces(line).empty()) {
      ++actual;
    }
  }

  bool ok = false;
  std::string spec_desc;
  if (spec == "+") {
    ok = actual >= 1;
    spec_desc = ">= 1";
  } else if (spec == "-") {
    ok = actual == 0;
    spec_desc = "== 0";
  } else {
    bool numeric = !spec.empty() && std::all_of(spec.begin(), spec.end(), [](char c) { return std::isdigit(c); });
    if (!numeric) {
      error_stream << "Invalid count spec \"" << spec << "\" (expected non-negative integer, '+' or '-')" << std::endl;
      return false;
    }
    size_t expected_count = std::stoul(spec);
    ok = actual == expected_count;
    spec_desc = "== " + std::to_string(expected_count);
  }
  if (!ok) {
    error_stream << "Your Result:" << std::endl;
    error_stream << result << std::endl;
    error_stream << "Row count check failure: expected " << spec_desc << ", got " << actual << std::endl;
  }
  return ok;
}

bool CompareResult(const std::string &result, const std::string &expected_result, SortMode sort_mode,
                   std::ostringstream &error_stream) {
  if (sort_mode == SortMode::KEYWORDS) {
    return CompareResultByKeywords(result, expected_result, error_stream);
  }
  if (sort_mode == SortMode::COUNT) {
    return CompareResultByCount(result, expected_result, error_stream);
  }
  auto result_lines = huadb::StringUtil::Split(result, '\n');
  auto expected_lines = huadb::StringUtil::Split(expected_result, '\n');
  if (sort_mode == SortMode::ROW_SORT) {
    std::sort(result_lines.begin(), result_lines.end());
    std::sort(expected_lines.begin(), expected_lines.end());
  }
  bool correct = (result_lines == expected_lines);
  if (!correct) {
    error_stream << "Your Result:" << std::endl;
    error_stream << result << std::endl;
    std::ofstream result_stream("../yours.log");
    for (const auto &line : result_lines) {
      result_stream << line << std::endl;
    }

    error_stream << "Expected Result:" << std::endl;
    error_stream << expected_result;
    std::ofstream expected_stream("../expected.log");
    for (const auto &line : expected_lines) {
      expected_stream << line << std::endl;
    }
  }
  return correct;
}

bool Run(const fs::path &path) {
  SQLLogicParser parser;
  bool success = parser.OpenFile(path);
  if (!success) {
    std::cerr << "Cannot open test script " << path << std::endl;
    exit(1);
  }
  parser.Parse();

  if (path.string().find("[hidden]") != std::string::npos) {
    for (auto &record : parser.records) {
      auto &file = record->loc_.file_;
      auto h = file.find("[hidden]");
      if (h != std::string::npos) {
        auto dot = file.rfind('.');
        std::string ext = (dot != std::string::npos) ? file.substr(dot) : "";
        file = file.substr(0, h) + file.substr(h + 8, 2) + "-[hidden]" + ext;
      }
    }
  }

  connections.clear();
  auto database = std::make_unique<huadb::DatabaseEngine>();

  for (const auto &record : parser.records) {
    switch (record->type_) {
      case RecordType::STATEMENT: {
        const auto &statement = dynamic_cast<const StatementRecord &>(*record);
        std::ostringstream result;
        auto writer = huadb::SimpleWriter(result, true);
        if (connections.find(statement.connection_name_) == connections.end()) {
          connections[statement.connection_name_] = std::make_unique<huadb::Connection>(*database);
        }
        // crash/flush/restart 是框架元命令，不走 SendQuery，也不需要 timeout。
        if (statement.sql_.substr(0, 5) == "crash") {
          database->Crash();
          break;
        }
        if (statement.sql_.substr(0, 5) == "flush") {
          database->Flush();
          break;
        }
        if (statement.sql_.substr(0, 7) == "restart") {
          database.reset();
          database = std::make_unique<huadb::DatabaseEngine>();
          connections.clear();
          break;
        }

        auto *conn_ptr = connections[statement.connection_name_].get();
        const std::string &sql = statement.sql_;

        std::mutex mu;
        std::condition_variable cv;
        bool finished = false;
        std::exception_ptr exc;

        std::thread worker([&]() {
          try {
            conn_ptr->SendQuery(sql, writer);
          } catch (...) {
            exc = std::current_exception();
          }
          {
            std::lock_guard<std::mutex> lk(mu);
            finished = true;
          }
          cv.notify_all();
        });

        int timeout = statement.timeout_seconds_ > 0 ? statement.timeout_seconds_ : DEFAULT_TIMEOUT_SECONDS;
        bool timed_out = false;
        {
          std::unique_lock<std::mutex> lk(mu);
          if (!cv.wait_for(lk, std::chrono::seconds(timeout), [&]() { return finished; })) {
            timed_out = true;
          }
        }

        if (timed_out) {
          // 同 QUERY 路径：worker 仍持有 Connection / DatabaseEngine 引用，
          // 直接销毁会 UAF，改为退出进程由评测器视为失败。
          std::cerr << record->loc_ << "\nStatement timed out after "
                    << timeout << " seconds" << std::endl;
          worker.detach();
          _exit(1);
        }
        worker.join();

        if (exc) {
          try {
            std::rethrow_exception(exc);
          } catch (huadb::DbException &e) {
            if (statement.expected_result_ == ResultType::SUCCESS) {
              std::cerr << record->loc_ << "\nUnexpected error: " << e.what() << std::endl;
              return false;
            }
          }
        } else {
          if (statement.expected_result_ == ResultType::ERROR) {
            std::cerr << record->loc_ << "\nUnexpected success" << std::endl;
            return false;
          }
        }
        break;
      }
      case RecordType::QUERY: {
        const auto &query = dynamic_cast<const QueryRecord &>(*record);
        std::ostringstream result;
        auto writer = huadb::SimpleWriter(result, true);
        if (connections.find(query.connection_name_) == connections.end()) {
          connections[query.connection_name_] = std::make_unique<huadb::Connection>(*database);
        }
        auto *conn_ptr = connections[query.connection_name_].get();
        const std::string &sql = query.sql_;

        std::mutex mu;
        std::condition_variable cv;
        bool finished = false;
        std::exception_ptr exc;

        std::thread worker([&]() {
          try {
            conn_ptr->SendQuery(sql, writer);
          } catch (...) {
            exc = std::current_exception();
          }
          {
            std::lock_guard<std::mutex> lk(mu);
            finished = true;
          }
          cv.notify_all();
        });

        int timeout = query.timeout_seconds_ > 0 ? query.timeout_seconds_ : DEFAULT_TIMEOUT_SECONDS;
        bool timed_out = false;
        {
          std::unique_lock<std::mutex> lk(mu);
          if (!cv.wait_for(lk, std::chrono::seconds(timeout), [&]() { return finished; })) {
            timed_out = true;
          }
        }

        if (timed_out) {
          // 子线程仍在跑，且持有 Connection / DatabaseEngine 的引用；
          // 此时直接销毁局部状态会造成 UAF，故直接退出进程，
          // 由评测器以 "测试超时" 认定失败即可。
          std::cerr << record->loc_ << "\nQuery timed out after "
                    << timeout << " seconds" << std::endl;
          worker.detach();
          _exit(1);
        }
        worker.join();

        if (exc) {
          try {
            std::rethrow_exception(exc);
          } catch (huadb::DbException &e) {
            std::cerr << record->loc_ << "\nUnexpected error: " << e.what() << std::endl;
            return false;
          }
        }

        std::ostringstream error_stream;
        if (!CompareResult(result.str(), query.expected_result_, query.sort_mode_, error_stream)) {
          std::cerr << record->loc_ << "\nUnexpected error: Wrong Result\n" << error_stream.str() << std::endl;
          return false;
        }
        break;
      }
      case RecordType::VERIFY_BTREE: {
        const auto &verify = dynamic_cast<const VerifyBtreeRecord &>(*record);

        // 复用默认 connection 跑 SHOW <idx_name> —— B+ 树是数据库级对象，
        // 与连接无关，无需暴露 verify btree 的 connection 参数。
        const std::string default_conn;
        if (connections.find(default_conn) == connections.end()) {
          connections[default_conn] = std::make_unique<huadb::Connection>(*database);
        }
        auto *conn_ptr = connections[default_conn].get();

        std::ostringstream result;
        auto writer = huadb::SimpleWriter(result, true);
        const std::string sql = "show " + verify.idx_name_ + ";";
        try {
          conn_ptr->SendQuery(sql, writer);
        } catch (huadb::DbException &e) {
          std::cerr << record->loc_
                    << "\nverify btree: SHOW " << verify.idx_name_ << " threw: " << e.what() << std::endl;
          return false;
        }

        auto pr = huadb::btree_test::ParsePrintOutput(result.str());
        if (!pr.ok) {
          std::cerr << record->loc_
                    << "\nverify btree: failed to parse SHOW output: " << pr.msg
                    << "\nRaw output:\n" << result.str() << std::endl;
          return false;
        }
        std::string outcome = huadb::btree_test::Verify(pr.tree, verify.expected_height_);
        if (outcome != "OK") {
          std::cerr << record->loc_
                    << "\nverify btree " << verify.idx_name_ << ": " << outcome
                    << "\nSHOW output:\n" << result.str() << std::endl;
          return false;
        }
        break;
      }
      default: {
        std::cerr << "Unknown record type" << std::endl;
        return false;
      }
    }
  }
  return true;
}

void ReportResult(const std::vector<std::string> &success_cases, const std::vector<std::string> &fail_cases) {
  std::ofstream report("report.json");
  report << "{\"success_cases\": [";
  for (size_t i = 0; i < success_cases.size(); i++) {
    report << "\"" << success_cases[i] << "\"";
    if (i != success_cases.size() - 1) {
      report << ", ";
    }
  }
  report << "], \"fail_cases\": [";
  for (size_t i = 0; i < fail_cases.size(); i++) {
    report << "\"" << fail_cases[i] << "\"";
    if (i != fail_cases.size() - 1) {
      report << ", ";
    }
  }
  report << "]}";
}

int main(int argc, char *argv[]) {
  argparse::ArgumentParser program("sqllogictest");
  program.add_argument("-c", "--count")
      .help("Number of times to run the tests")
      .default_value(1u)
      .metavar("COUNT")
      .scan<'u', unsigned>();
  program.add_argument("-o", "--output").help("Report the result of the test").flag();
  program.add_argument("test_files").help("Test files to run").nargs(argparse::nargs_pattern::at_least_one);

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
    std::exit(1);
  }

  bool report_result = program.get<bool>("-o");
  int test_count = program.get<unsigned>("-c");
  auto test_files = program.get<std::vector<std::string>>("test_files");

  std::vector<fs::path> paths;
  for (const auto &file : test_files) {
    paths.emplace_back(fs::absolute(file));
  }

  bool success = true;
  std::vector<std::string> success_cases;
  std::vector<std::string> fail_cases;
  auto initial_dir = fs::current_path();

  for (unsigned i = 0; i < test_count; i++) {
    success_cases.clear();
    fail_cases.clear();

    for (const auto &path : paths) {
      auto testcase_name = path.parent_path().filename().string() + "/" + path.stem().string();
      auto hidden_start = testcase_name.find("[hidden]");
      if (hidden_start == std::string::npos) {
        std::cout << testcase_name << " " << std::flush;
      } else {
        std::cout << testcase_name.substr(0, hidden_start) + testcase_name.substr(hidden_start + 8, 2) + "-[hidden]" << " " << std::flush;
      }

      bool verbose_disabled = IsVerboseDisabled(path);

      pid_t pid = fork();
      if (pid == 0) {
        if (verbose_disabled) {
          freopen("/dev/null", "w", stderr);
        }
        fs::current_path(initial_dir);
        if (fs::is_directory(TEST_DIRECTORY)) {
          fs::remove_all(TEST_DIRECTORY);
        }
        fs::create_directory(TEST_DIRECTORY);
        fs::current_path(TEST_DIRECTORY);
        try {
          bool passed = Run(path);
          _exit(passed ? 0 : 1);
        } catch (std::exception &e) {
          std::cerr << e.what() << std::endl;
          _exit(1);
        } catch (...) {
          std::cerr << "Unknown exception" << std::endl;
          _exit(1);
        }
      }
      if (pid < 0) {
        std::cerr << "fork failed" << std::endl;
        return 1;
      }

      int status;
      waitpid(pid, &status, 0);
      bool passed = WIFEXITED(status) && WEXITSTATUS(status) == 0;
      if (passed) {
        std::cout << huadb::BOLD << huadb::GREEN << "PASS" << huadb::RESET << std::endl;
        success_cases.push_back(testcase_name);
      } else {
        std::cout << huadb::BOLD << huadb::RED << "ERROR" << huadb::RESET << std::endl;
        success = false;
        fail_cases.push_back(testcase_name);
        if (!verbose_disabled && WIFSIGNALED(status)) {
          std::cerr << "Test killed by signal " << WTERMSIG(status) << std::endl;
        }
      }
    }
    fs::current_path(initial_dir);
    if (!success) {
      break;
    }
  }

  if (report_result) {
    ReportResult(success_cases, fail_cases);
  }

  if (success) {
    return 0;
  } else {
    return 1;
  }
}
