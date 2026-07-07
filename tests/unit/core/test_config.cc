/**
 * @file test_config.cc
 * @brief Config模块全面功能测试 (GoogleTest 迁移)
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/core/config/config.h"
#include "zero/core/log/log.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <cstring>
#include <thread>
#include <set>

using namespace zero;

static std::string tmpDir() {
    std::string d = "/tmp/config_test_" + std::to_string(getpid());
    mkdir(d.c_str(), 0755);
    return d;
}

static void writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
    f.close();
}

static void rmDir(const std::string& dir) {
    std::string cmd = "rm -rf " + dir;
    if(system(cmd.c_str()) < 0) {
        // ignore
    }
}

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto root = ZERO_LOG_ROOT();
        root->setLevel(LogLevel::FATAL);
    }
};

TEST_F(ConfigTest, ConfigVarBase) {
    auto var = Config::Lookup<int>("test.base.int", 42, "test int");
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->getName(), "test.base.int");
    EXPECT_EQ(var->getDescription(), "test int");
    EXPECT_FALSE(var->getTypeName().empty());
    EXPECT_FALSE(var->toString().empty());
}

TEST_F(ConfigTest, ConfigVarGetSet) {
    auto var = Config::Lookup<int>("test.v.int", 10);
    EXPECT_EQ(var->getValue(), 10);
    var->setValue(20);
    EXPECT_EQ(var->getValue(), 20);
    var->setValue(20);
    EXPECT_EQ(var->getValue(), 20);
}

TEST_F(ConfigTest, ConfigVarListeners) {
    auto var = Config::Lookup<int>("test.listener.int", 0);
    int old_v = -1, new_v = -1;
    int call_count = 0;

    uint64_t key = var->addListener([&](const int& o, const int& n) {
        old_v = o; new_v = n; ++call_count;
    });
    EXPECT_NE(key, 0u);

    var->setValue(100);
    EXPECT_EQ(old_v, 0);
    EXPECT_EQ(new_v, 100);
    EXPECT_EQ(call_count, 1);

    var->setValue(100);
    EXPECT_EQ(call_count, 1);

    auto cb = var->getListener(key);
    ASSERT_NE(cb, nullptr);
    cb(100, 200);
    EXPECT_EQ(call_count, 2);

    var->delListener(key);
    EXPECT_EQ(var->getListener(key), nullptr);

    var->addListener([&](const int&, const int&) { ++call_count; });
    var->addListener([&](const int&, const int&) { ++call_count; });
    var->clearListener();
    int before = call_count;
    var->setValue(300);
    EXPECT_EQ(call_count, before);
}

TEST_F(ConfigTest, RangeValidator) {
    RangeValidator<int> v(0, 100, true);
    EXPECT_TRUE(v.validate(50).valid);
    EXPECT_TRUE(v.validate(0).valid);
    EXPECT_TRUE(v.validate(100).valid);
    EXPECT_FALSE(v.validate(-1).valid);
    EXPECT_FALSE(v.validate(101).valid);
    EXPECT_FALSE(v.validate(-1).message.empty());
}

TEST_F(ConfigTest, RangeValidatorExclusive) {
    RangeValidator<int> v(0, 100, false);
    EXPECT_TRUE(v.validate(50).valid);
    EXPECT_FALSE(v.validate(0).valid);
    EXPECT_FALSE(v.validate(100).valid);
}

TEST_F(ConfigTest, EnumValidator) {
    EnumValidator<std::string> v({"debug", "info", "warn", "error"});
    EXPECT_TRUE(v.validate("info").valid);
    EXPECT_TRUE(v.validate("error").valid);
    EXPECT_FALSE(v.validate("trace").valid);
    EXPECT_FALSE(v.validate("").valid);
}

TEST_F(ConfigTest, RegexValidator) {
    RegexValidator v("[a-zA-Z][a-zA-Z0-9]*", "identifier");
    EXPECT_TRUE(v.validate("hello123").valid);
    EXPECT_TRUE(v.validate("x").valid);
    EXPECT_FALSE(v.validate("123abc").valid);
    EXPECT_FALSE(v.validate("").valid);
    auto r = v.validate("123abc");
    EXPECT_FALSE(r.message.empty());
}

TEST_F(ConfigTest, CustomValidator) {
    CustomValidator<int> v([](int val) -> ConfigValidateResult {
        ConfigValidateResult r;
        if (val % 2 == 0) { r.valid = true; r.message = ""; }
        else { r.valid = false; r.message = "must be even"; }
        return r;
    }, "even_check");
    EXPECT_TRUE(v.validate(2).valid);
    EXPECT_TRUE(v.validate(0).valid);
    EXPECT_FALSE(v.validate(1).valid);
    EXPECT_EQ(v.validate(1).message, "must be even");
}

TEST_F(ConfigTest, ConfigVarWithValidators) {
    auto var = Config::Lookup<int>("test.validated.int", 50);
    var->addValidator(std::make_shared<RangeValidator<int>>(0, 100));

    EXPECT_TRUE(var->validate(50).valid);
    EXPECT_FALSE(var->validate(200).valid);

    EXPECT_TRUE(var->trySetValue(80));
    EXPECT_EQ(var->getValue(), 80);

    EXPECT_FALSE(var->trySetValue(200));
    EXPECT_EQ(var->getValue(), 80);

    var->setValue(200);
    EXPECT_EQ(var->getValue(), 200);

    EXPECT_EQ(var->getValidators().size(), 1u);
    var->clearValidators();
    EXPECT_EQ(var->getValidators().size(), 0u);
}

TEST_F(ConfigTest, ReadOnly) {
    auto var = Config::Lookup<int>("test.ro.int", 100);
    EXPECT_FALSE(var->isReadOnly());
    var->setReadOnly(true);
    EXPECT_TRUE(var->isReadOnly());
    var->setValue(200);
    EXPECT_EQ(var->getValue(), 100);
    EXPECT_FALSE(var->trySetValue(200));
    EXPECT_EQ(var->getValue(), 100);
}

TEST_F(ConfigTest, LookupCreateFind) {
    auto v1 = Config::Lookup<int>("test.lookup.int", 42, "desc");
    ASSERT_NE(v1, nullptr);
    EXPECT_EQ(v1->getValue(), 42);

    auto v2 = Config::Lookup<int>("test.lookup.int", 99);
    ASSERT_NE(v2, nullptr);
    EXPECT_EQ(v2->getValue(), 42);

    auto v3 = Config::Lookup<int>("test.lookup.nonexist");
    EXPECT_EQ(v3, nullptr);
}

TEST_F(ConfigTest, LookupTypeMismatch) {
    Config::Lookup<int>("test.mismatch", 42);
    auto v = Config::Lookup<std::string>("test.mismatch");
    EXPECT_EQ(v, nullptr);
}

TEST_F(ConfigTest, LookupInvalidName) {
    EXPECT_THROW(Config::Lookup<int>("invalid name!", 0), std::invalid_argument);
    EXPECT_THROW(Config::Lookup<int>("invalid@name", 0), std::invalid_argument);
}

TEST_F(ConfigTest, HasSet) {
    Config::Lookup<int>("test.has.int", 1);
    EXPECT_TRUE(Config::Has("test.has.int"));
    EXPECT_FALSE(Config::Has("test.has.nonexist"));

    Config::Set<int>("test.set.int", 999);
    EXPECT_EQ(Config::Lookup<int>("test.set.int")->getValue(), 999);

    Config::Set<std::string>("test.set.str", "hello");
    EXPECT_EQ(Config::Lookup<std::string>("test.set.str")->getValue(), "hello");

    Config::Set<double>("test.set.dbl", 3.14);
    EXPECT_NE(Config::Lookup<double>("test.set.dbl"), nullptr);
}

TEST_F(ConfigTest, SizeAndNames) {
    Config::Lookup<int>("test.size.a", 1);
    Config::Lookup<int>("test.size.b", 2);
    size_t sz = Config::Size();
    EXPECT_GE(sz, 2u);
    auto names = Config::GetAllNames();
    bool found_a = false, found_b = false;
    for (auto& n : names) {
        if (n == "test.size.a") found_a = true;
        if (n == "test.size.b") found_b = true;
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
}

TEST_F(ConfigTest, LookupBase) {
    Config::Lookup<int>("test.base.int", 10);
    auto base = Config::LookupBase("test.base.int");
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->getName(), "test.base.int");
    EXPECT_EQ(Config::LookupBase("test.base.nonexist"), nullptr);
}

TEST_F(ConfigTest, LoadFromYamlScalar) {
    Config::Set<int>("test.yaml.int", 0);
    auto var = Config::Lookup<int>("test.yaml.int");
    YAML::Node root = YAML::Load("test:\n  yaml:\n    int: 777\n");
    Config::LoadFromYaml(root);
    EXPECT_EQ(var->getValue(), 777);
}

TEST_F(ConfigTest, LoadFromYamlNested) {
    Config::Set<int>("test.yaml.a.b.c", 0);
    auto var = Config::Lookup<int>("test.yaml.a.b.c");
    YAML::Node root = YAML::Load("test:\n  yaml:\n    a:\n      b:\n        c: 42\n");
    Config::LoadFromYaml(root);
    EXPECT_EQ(var->getValue(), 42);
}

TEST_F(ConfigTest, LoadFromYamlString) {
    Config::Set<std::string>("test.yaml.str", "");
    auto var = Config::Lookup<std::string>("test.yaml.str");
    YAML::Node root = YAML::Load("test:\n  yaml:\n    str: \"hello yaml\"\n");
    Config::LoadFromYaml(root);
    EXPECT_EQ(var->getValue(), "hello yaml");
}

TEST_F(ConfigTest, LoadFromJson) {
    Config::Set<int>("test.json.int", 0);
    auto var = Config::Lookup<int>("test.json.int");
    std::string json = "test:\n  json:\n    int: 555\n";
    Config::LoadFromJson(json);
    EXPECT_EQ(var->getValue(), 555);
}

TEST_F(ConfigTest, LoadFromCmdArgs) {
    Config::Set<int>("test.cmd.port", 0);
    Config::Set<std::string>("test.cmd.host", "");
    auto port_var = Config::Lookup<int>("test.cmd.port");
    auto host_var = Config::Lookup<std::string>("test.cmd.host");
    char* argv[] = {
        (char*)"test_app",
        (char*)"--test.cmd.port=8080",
        (char*)"--test.cmd.host=localhost",
    };
    int argc = 3;
    Config::LoadFromCmdArgs(argc, argv);
    EXPECT_EQ(port_var->getValue(), 8080);
    EXPECT_EQ(host_var->getValue(), "localhost");
}

TEST_F(ConfigTest, DumpToYaml) {
    Config::Set<int>("test.dump.a", 1);
    Config::Set<std::string>("test.dump.b", "hello");
    std::string dump = Config::DumpToYaml();
    EXPECT_FALSE(dump.empty());
    EXPECT_NE(dump.find("test"), std::string::npos);
}

TEST_F(ConfigTest, Visit) {
    Config::Set<int>("test.visit.a", 1);
    Config::Set<int>("test.visit.b", 2);
    int count = 0;
    Config::Visit([&](ConfigVarBase::ptr var) {
        if (var->getName().find("test.visit") == 0) ++count;
    });
    EXPECT_GE(count, 2);
}

TEST_F(ConfigTest, ProfilesRegisterAndGet) {
    ConfigProfile dev;
    dev.name = "development";
    dev.description = "dev env";
    dev.overrides["test.profile.val"] = "dev_value";
    Config::RegisterProfile(dev);
    auto profiles = Config::GetProfiles();
    EXPECT_GE(profiles.size(), 1u);
    bool found = false;
    for (auto& p : profiles) {
        if (p.name == "development") { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(ConfigTest, ProfilesActivate) {
    Config::Set<std::string>("test.profile.activate", "original");
    ConfigProfile prod;
    prod.name = "production_test";
    prod.overrides["test.profile.activate"] = "prod_value";
    Config::RegisterProfile(prod);
    EXPECT_TRUE(Config::ActivateProfile("production_test"));
    auto var = Config::Lookup<std::string>("test.profile.activate");
    EXPECT_EQ(var->getValue(), "prod_value");
    EXPECT_FALSE(Config::ActivateProfile("nonexistent_profile"));
}

TEST_F(ConfigTest, ProfilesParentInheritance) {
    Config::Set<std::string>("test.profile.inherit.a", "base");
    Config::Set<std::string>("test.profile.inherit.b", "base");
    ConfigProfile base;
    base.name = "base_profile";
    base.overrides["test.profile.inherit.a"] = "base_val";
    ConfigProfile child;
    child.name = "child_profile";
    child.parent = "base_profile";
    child.overrides["test.profile.inherit.b"] = "child_val";
    Config::RegisterProfile(base);
    Config::RegisterProfile(child);
    Config::ActivateProfile("child_profile");
    auto va = Config::Lookup<std::string>("test.profile.inherit.a");
    auto vb = Config::Lookup<std::string>("test.profile.inherit.b");
    EXPECT_EQ(va->getValue(), "base_val");
    EXPECT_EQ(vb->getValue(), "child_val");
}

TEST_F(ConfigTest, ChangeHistory) {
    Config::ClearChangeHistory();
    auto hist = Config::GetChangeHistory(10);
    EXPECT_LE(hist.size(), 10u);
    Config::ClearChangeHistory();
    auto hist2 = Config::GetChangeHistory(10);
    EXPECT_LE(hist2.size(), 10u);
}

TEST_F(ConfigTest, ValidateAll) {
    auto failures = Config::ValidateAll();
    (void)failures;
    SUCCEED();
}

TEST_F(ConfigTest, LexicalCastInt) {
    LexicalCast<std::string, int> c;
    EXPECT_EQ(c("42"), 42);
    EXPECT_EQ(c("-1"), -1);
    EXPECT_EQ(c("0"), 0);
}

TEST_F(ConfigTest, LexicalCastString) {
    LexicalCast<std::string, std::string> c;
    EXPECT_EQ(c("hello"), "hello");
    EXPECT_EQ(c(""), "");
}

TEST_F(ConfigTest, LexicalCastDouble) {
    LexicalCast<std::string, double> c;
    double v = c("3.14");
    EXPECT_GT(v, 3.13);
    EXPECT_LT(v, 3.15);
}

TEST_F(ConfigTest, LexicalCastBool) {
    LexicalCast<std::string, bool> c;
    EXPECT_EQ(c("1"), true);
    EXPECT_EQ(c("0"), false);
}

TEST_F(ConfigTest, LexicalCastVector) {
    LexicalCast<std::string, std::vector<int>> c;
    auto v = c("[1, 2, 3]");
    EXPECT_EQ(v.size(), 3u);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[2], 3);
    LexicalCast<std::vector<int>, std::string> c2;
    std::string s = c2(v);
    EXPECT_FALSE(s.empty());
}

TEST_F(ConfigTest, LexicalCastList) {
    LexicalCast<std::string, std::list<int>> c;
    auto v = c("[10, 20, 30]");
    EXPECT_EQ(v.size(), 3u);
    LexicalCast<std::list<int>, std::string> c2;
    std::string s = c2(v);
    EXPECT_FALSE(s.empty());
}

TEST_F(ConfigTest, LexicalCastSet) {
    LexicalCast<std::string, std::set<int>> c;
    auto v = c("[5, 10, 15]");
    EXPECT_EQ(v.size(), 3u);
    EXPECT_TRUE(v.find(5) != v.end());
    LexicalCast<std::set<int>, std::string> c2;
    std::string s = c2(v);
    EXPECT_FALSE(s.empty());
}

TEST_F(ConfigTest, LexicalCastUnorderedSet) {
    LexicalCast<std::string, std::unordered_set<int>> c;
    auto v = c("[100, 200]");
    EXPECT_EQ(v.size(), 2u);
    LexicalCast<std::unordered_set<int>, std::string> c2;
    std::string s = c2(v);
    EXPECT_FALSE(s.empty());
}

TEST_F(ConfigTest, LexicalCastMap) {
    LexicalCast<std::string, std::map<std::string, int>> c;
    auto v = c("{a: 1, b: 2}");
    EXPECT_TRUE(v.find("a") != v.end());
    EXPECT_EQ(v["a"], 1);
    LexicalCast<std::map<std::string, int>, std::string> c2;
    std::string s = c2(v);
    EXPECT_FALSE(s.empty());
}

TEST_F(ConfigTest, LexicalCastUnorderedMap) {
    LexicalCast<std::string, std::unordered_map<std::string, int>> c;
    auto v = c("{x: 100, y: 200}");
    EXPECT_TRUE(v.find("x") != v.end());
    EXPECT_EQ(v["x"], 100);
    LexicalCast<std::unordered_map<std::string, int>, std::string> c2;
    std::string s = c2(v);
    EXPECT_FALSE(s.empty());
}

TEST_F(ConfigTest, HotReloadFileChange) {
    std::string dir = tmpDir();
    std::string conf_file = dir + "/test.yml";
    auto var = Config::Lookup<std::string>("test.hot.value", "default", "hot reload test");
    writeFile(conf_file, "test:\n  hot:\n    value: \"initial\"\n");
    Config::LoadFromConfDir(dir, true);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->getValue(), "initial");
    bool started = Config::StartWatch(dir);
    EXPECT_TRUE(started);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    writeFile(conf_file, "test:\n  hot:\n    value: \"updated\"\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    EXPECT_EQ(var->getValue(), "updated");
    Config::StopWatch();
    rmDir(dir);
}

TEST_F(ConfigTest, HotReloadNoRealChange) {
    std::string dir = tmpDir();
    std::string conf_file = dir + "/nochange.yml";
    auto var = Config::Lookup<std::string>("test.nochg.val", "default");
    writeFile(conf_file, "test:\n  nochg:\n    val: \"same\"\n");
    Config::LoadFromConfDir(dir, true);
    EXPECT_EQ(var->getValue(), "same");
    Config::StartWatch(dir);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    writeFile(conf_file, "test:\n  nochg:\n    val: \"same\"\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(var->getValue(), "same");
    Config::StopWatch();
    rmDir(dir);
}

TEST_F(ConfigTest, HotReloadAddNewConfig) {
    std::string dir = tmpDir();
    std::string f1 = dir + "/existing.yml";
    std::string f2 = dir + "/newfile.yml";
    auto existing_var = Config::Lookup<std::string>("test.hotadd.existing", "default");
    auto var = Config::Lookup<std::string>("test.hotadd.newone", "default");
    writeFile(f1, "test:\n  hotadd:\n    existing: \"old\"\n");
    Config::LoadFromConfDir(dir, true);
    Config::StartWatch(dir);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    writeFile(f2, "test:\n  hotadd:\n    newone: \"fresh\"\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->getValue(), "fresh");
    Config::StopWatch();
    rmDir(dir);
}

TEST_F(ConfigTest, HotReloadMultipleChangesDebounced) {
    std::string dir = tmpDir();
    std::string cf = dir + "/debounce.yml";
    auto var = Config::Lookup<std::string>("test.debounce.val", "default");
    writeFile(cf, "test:\n  debounce:\n    val: \"v0\"\n");
    Config::LoadFromConfDir(dir, true);
    Config::StartWatch(dir);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    writeFile(cf, "test:\n  debounce:\n    val: \"v1\"\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    writeFile(cf, "test:\n  debounce:\n    val: \"v2\"\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    writeFile(cf, "test:\n  debounce:\n    val: \"v3\"\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    EXPECT_EQ(var->getValue(), "v3");
    Config::StopWatch();
    rmDir(dir);
}

TEST_F(ConfigTest, HotReloadStop) {
    std::string dir = tmpDir();
    std::string cf = dir + "/stop.yml";
    auto var = Config::Lookup<std::string>("test.stop.val", "default");
    writeFile(cf, "test:\n  stop:\n    val: \"before\"\n");
    Config::LoadFromConfDir(dir, true);
    Config::StartWatch(dir);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Config::StopWatch();
    writeFile(cf, "test:\n  stop:\n    val: \"after\"\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(var->getValue(), "before");
    rmDir(dir);
}

TEST_F(ConfigTest, HotReloadTempFilesIgnored) {
    std::string dir = tmpDir();
    std::string cf = dir + "/real.yml";
    auto var = Config::Lookup<std::string>("test.tempignore.val", "default");
    writeFile(cf, "test:\n  tempignore:\n    val: \"real_value\"\n");
    Config::LoadFromConfDir(dir, true);
    Config::StartWatch(dir);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    writeFile(dir + "/.editor.swp", "garbage");
    writeFile(dir + "/tmp.yml~", "garbage");
    writeFile(dir + "/#backup.yml", "garbage");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(var->getValue(), "real_value");
    Config::StopWatch();
    rmDir(dir);
}

TEST_F(ConfigTest, EdgeCases) {
    auto v1 = Config::Lookup<std::string>("test.edge.empty", "");
    EXPECT_EQ(v1->getValue(), "");
    auto v2 = Config::Lookup<uint64_t>("test.edge.big", UINT64_MAX);
    EXPECT_EQ(v2->getValue(), UINT64_MAX);
    auto v3 = Config::Lookup<int>("test.edge.neg", -999);
    EXPECT_EQ(v3->getValue(), -999);
    Config::Lookup<int>("test.edge.baseok", 1);
    auto base = Config::LookupBase("test.edge.baseok");
    ASSERT_NE(base, nullptr);
}

TEST_F(ConfigTest, SetReadOnlyViaConfigClass) {
    Config::Lookup<int>("test.ro.class", 50);
    Config::SetReadOnly<int>("test.ro.class", true);
    auto var = Config::Lookup<int>("test.ro.class");
    EXPECT_TRUE(var->isReadOnly());
    Config::SetReadOnly<int>("test.ro.class", false);
    EXPECT_FALSE(var->isReadOnly());
}

TEST_F(ConfigTest, AddValidatorViaConfigClass) {
    Config::Lookup<int>("test.validator.class", 10);
    auto v = std::make_shared<RangeValidator<int>>(0, 50);
    Config::AddValidator<int>("test.validator.class", v);
    auto var = Config::Lookup<int>("test.validator.class");
    EXPECT_EQ(var->getValidators().size(), 1u);
    var->clearValidators();
}

TEST_F(ConfigTest, ConcurrentAccess) {
    auto var = Config::Lookup<int>("test.concurrent.int", 0);
    std::atomic<bool> running{true};
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t]() {
            while (running.load(std::memory_order_acquire)) {
                try {
                    var->setValue(t);
                    int v = var->getValue();
                    var->toString();
                    var->validate(v);
                } catch (...) {
                    errors.fetch_add(1);
                }
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    running.store(false);
    for (auto& th : threads) th.join();
    EXPECT_EQ(errors.load(), 0);
}
