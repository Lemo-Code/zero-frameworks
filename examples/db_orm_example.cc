/**
 * @file db_orm_example.cc
 * @brief ORM 框架产品级示例
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <iostream>
#include "zero/db/db_value.h"
#include "zero/db/db_query.h"
#include "zero/db/db_model.h"
#include "zero/db/db_repository.h"
#include "zero/db/db_manager.h"
#include "zero/db/db_transaction.h"
#include "zero/db/db_schema.h"
#include "zero/db/db_relation.h"
#include "zero/db/db_session_hook.h"

#ifdef HAS_MYSQL
#include "zero/db/mysql/mysql_driver.h"
#include "zero/core/concurrency/scheduler.h"
#endif

using namespace zero::db;

// ============================================================================
// 业务模型
// ============================================================================
class Account : public DbModel {
public:
    int64_t id = 0;
    std::string username;
    int64_t balance = 0;
    std::string createdAt;

    ZERO_DB_MODEL(Account, "account")
    ZERO_DB_FIELD_INT64(id, "id", PrimaryKey | AutoIncrement)
    ZERO_DB_PRIMARY_KEY("id")
    ZERO_DB_AUTO_INCREMENT()
    ZERO_DB_FIELD_STRING(username, "username", NotNull | Unique)
    ZERO_DB_FIELD_INT64(balance, "balance", 0)
    ZERO_DB_FIELD_STRING(createdAt, "created_at", 0)
    ZERO_DB_MODEL_END()
};

class Article : public DbModel {
public:
    int64_t id = 0;
    std::string title;
    int64_t userId = 0;
    std::string createdAt;
    std::string updatedAt;
    std::string deletedAt;
    int64_t version = 0;

    ZERO_DB_MODEL(Article, "article")
    ZERO_DB_FIELD_INT64(id, "id", PrimaryKey | AutoIncrement)
    ZERO_DB_PRIMARY_KEY("id")
    ZERO_DB_AUTO_INCREMENT()
    ZERO_DB_FIELD_STRING(title, "title", NotNull)
    ZERO_DB_FIELD_INT64(userId, "user_id", 0)
    ZERO_DB_AUTO_TIMESTAMP("created_at", "updated_at")
    ZERO_DB_SOFT_DELETE("deleted_at")
    ZERO_DB_VERSION("version")
    ZERO_DB_MODEL_END()
};

class ValidatedAccount : public DbModel {
public:
    int64_t id = 0;
    std::string email;
    std::string username;
    int32_t age = 0;
    int64_t balance = 0;

    ZERO_DB_MODEL(ValidatedAccount, "validated_account")
    ZERO_DB_FIELD_INT64(id, "id", PrimaryKey | AutoIncrement)
    ZERO_DB_PRIMARY_KEY("id")
    ZERO_DB_AUTO_INCREMENT()
    ZERO_DB_FIELD_STRING(email, "email", NotNull)
    ZERO_DB_VALIDATE_EMAIL("email")
    ZERO_DB_FIELD_STRING(username, "username", NotNull)
    ZERO_DB_VALIDATE_NOT_EMPTY("username")
    ZERO_DB_VALIDATE_MAX_LENGTH("username", 32)
    ZERO_DB_FIELD_INT32(age, "age", 0)
    ZERO_DB_VALIDATE_RANGE("age", 18, 120)
    ZERO_DB_FIELD_INT64(balance, "balance", 0)
    ZERO_DB_VALIDATE_MIN("balance", 0)
    ZERO_DB_MODEL_END()
};

class HookedAccount : public DbModel {
public:
    int64_t id = 0;
    std::string username;

    ZERO_DB_MODEL(HookedAccount, "hooked_account")
    ZERO_DB_FIELD_INT64(id, "id", PrimaryKey | AutoIncrement)
    ZERO_DB_PRIMARY_KEY("id")
    ZERO_DB_AUTO_INCREMENT()
    ZERO_DB_FIELD_STRING(username, "username", NotNull)
    ZERO_DB_MODEL_END()

    void afterSave() override {
        std::cout << "[HOOK] afterSave called, id=" << id << std::endl;
    }
    void afterLoad() override {
        std::cout << "[HOOK] afterLoad called, id=" << id << std::endl;
    }
};

class AuditLogger : public DbSessionHook {
public:
    void beforeExecute(const std::string& sql, const DbValues& params) override {
        std::cout << "[AUDIT EXECUTE] " << sql << " params=" << params.size() << std::endl;
    }
    void afterExecute(const std::string& sql, const DbValues& params,
                      bool success, uint64_t costMs) override {
        (void)sql; (void)params;
        std::cout << "[AUDIT RESULT] success=" << success << " cost=" << costMs << "ms" << std::endl;
    }
};

// ============================================================================
// DummySession
// ============================================================================
class DummySession : public DbSession {
public:
    bool execute(const std::string& sql, const DbValues& params = {}) override {
        std::cout << "[EXECUTE] " << sql << std::endl;
        for (size_t i = 0; i < params.size(); ++i) {
            std::cout << "  param[" << i << "]=" << params[i].toString() << std::endl;
        }
        return true;
    }

    DbResult query(const std::string& sql, const DbValues& params = {}) override {
        std::cout << "[QUERY] " << sql << std::endl;
        for (size_t i = 0; i < params.size(); ++i) {
            std::cout << "  param[" << i << "]=" << params[i].toString() << std::endl;
        }
        return DbResult();
    }

    int64_t lastInsertId() const override { return 0; }
    int64_t affectedRows() const override { return 1; }
    bool inTransaction() const override { return false; }
    bool beginTransaction() override { return true; }
    bool commit() override { return true; }
    bool rollback() override { return true; }
    std::shared_ptr<DbTransaction> openTransaction() override {
        return std::shared_ptr<DbTransaction>(new DbTransaction(
            std::static_pointer_cast<DbSession>(shared_from_this())));
    }
};

static void demo_query_builder(DbSession::ptr session) {
    std::cout << "\n=== QueryBuilder Demo ===" << std::endl;
    auto q = std::make_shared<DbQuery>(session, "account");
    q->select({"id", "username", "balance"})
      ->where("balance", ">=", DbValue::int64(0))
      ->andWhere("username", "like", DbValue::string("%test%"))
      ->whereNotIn("status", {DbValue::string("deleted"), DbValue::string("banned")})
      ->groupBy("username")
      ->having("SUM(`balance`) > ?", {DbValue::int64(100)})
      ->orderByDesc("id")
      ->limit(20);
    auto p = q->buildSelect();
    std::cout << "SQL: " << p.first << std::endl;
}

static void demo_repository(DbSession::ptr session) {
    std::cout << "\n=== Repository Demo ===" << std::endl;
    DbRepository<Account> repo(session);

    Account a;
    a.username = "zero_test";
    a.balance = 100;
    a.createdAt = "2026-07-06";
    repo.save(a);

    auto list = repo.whereEq("username", DbValue::string("zero_test"));
    std::cout << "find " << list.size() << " account(s)" << std::endl;
}

static void demo_advanced_model(DbSession::ptr session) {
    std::cout << "\n=== AutoTimestamp / SoftDelete / OptimisticLock Demo ===" << std::endl;
    DbRepository<Article> repo(session);

    Article article;
    article.title = "hello orm";
    article.userId = 1;
    repo.save(article);

    article.id = 1;
    article.version = 1;
    repo.updateWithVersion(article);

    repo.removeById("1");

    auto q = repo.query();
    auto p = q->buildSelect();
    std::cout << "SQL: " << p.first << std::endl;
}

static void demo_batch_and_page(DbSession::ptr session) {
    std::cout << "\n=== Batch & Page Demo ===" << std::endl;
    DbRepository<Account> repo(session);

    std::vector<Account> accounts;
    for (int i = 0; i < 5; ++i) {
        Account a;
        a.username = "batch_" + std::to_string(i);
        a.balance = i * 10;
        a.createdAt = "2026-07-06";
        accounts.push_back(a);
    }
    repo.batchInsert(accounts);

    PageInfo page;
    page.page = 1;
    page.pageSize = 10;
    auto q = repo.query()->where("balance", ">=", DbValue::int64(0));
    auto result = repo.findPage(page, q);
    std::cout << "page total=" << result.page.total
              << " totalPages=" << result.page.totalPages
              << " rows=" << result.rows.size() << std::endl;
}

static void demo_relation(DbSession::ptr session) {
    std::cout << "\n=== Relation Demo ===" << std::endl;
    auto articles = DbRelation<Account, Article>::hasMany(
        session, "user_id", "id", DbValue::int64(1));
    std::cout << "hasMany articles: " << articles.size() << std::endl;
}

static void demo_transaction(DbSession::ptr session) {
    std::cout << "\n=== Transaction Demo ===" << std::endl;
    DbTransactionGuard guard(session);
    auto q = std::make_shared<DbQuery>(session, "account");
    q->whereEq("id", DbValue::int64(1));
    q->update({{"balance", DbValue::int64(200)}});
    guard.commit();
}

static void demo_validator(DbSession::ptr session) {
    std::cout << "\n=== Validator Demo ===" << std::endl;
    DbRepository<ValidatedAccount> repo(session);

    ValidatedAccount bad;
    bad.email = "not-an-email";
    bad.username = "";
    bad.age = 10;
    bad.balance = -5;
    auto r = repo.save2(bad);
    if (!r.ok()) {
        std::cout << "validation failed: " << r.error.message << std::endl;
    }

    ValidatedAccount good;
    good.email = "alice@example.com";
    good.username = "alice";
    good.age = 25;
    good.balance = 100;
    repo.save(good);
    (void)session;
}

static void demo_hooks(DbSession::ptr session) {
    std::cout << "\n=== Model Hooks Demo ===" << std::endl;
    DbRepository<HookedAccount> repo(session);
    HookedAccount a;
    a.username = "hooked";
    repo.save(a);
}

static void demo_eager_load(DbSession::ptr session) {
    std::cout << "\n=== Eager Load Demo ===" << std::endl;
    std::vector<std::shared_ptr<Account>> accounts;
    for (int i = 0; i < 3; ++i) {
        auto a = std::make_shared<Account>();
        a->id = i + 1;
        accounts.push_back(a);
    }
    auto articles = DbRelation<Account, Article>::withMany(session, accounts, "id", "user_id");
    std::cout << "eager loaded " << articles.size() << " articles" << std::endl;
}

static void demo_schema(DbSession::ptr session) {
    std::cout << "\n=== Schema Demo ===" << std::endl;
    Article a;
    std::string sql = DbSchema::generateCreateTableSql(a.tableName(), a.fields());
    std::cout << sql << std::endl;
    (void)session;
}

#ifdef HAS_MYSQL
static void demo_mysql(const zero::MySQLPoolConfig& config) {
    std::cout << "\n=== MySQL Driver Demo ===" << std::endl;
    auto driver = zero::db::MySQLDriver::Create(config, zero::Scheduler::GetThis());
    auto session = driver->openSession();
    if (!session) {
        std::cerr << "open mysql session failed" << std::endl;
        return;
    }
    session->addHook(std::make_shared<AuditLogger>());
    demo_query_builder(session);
    demo_repository(session);
    demo_advanced_model(session);
    demo_batch_and_page(session);
    demo_relation(session);
    demo_transaction(session);
    demo_validator(session);
    demo_hooks(session);
    demo_eager_load(session);
    demo_schema(session);
    std::cout << "stats: " << driver->getStatsJson() << std::endl;
    driver->close();
}
#endif

int main(int argc, char** argv) {
    (void)argc; (void)argv;

#ifdef HAS_MYSQL
    zero::MySQLPoolConfig config;
    config.host = "127.0.0.1";
    config.port = 3306;
    config.user = "";
    config.password = "";
    config.database = "test";
    config.minConnections = 1;
    config.maxConnections = 4;
    config.workerThreads = 2;

    const char* envHost = std::getenv("ZERO_MYSQL_HOST");
    const char* envUser = std::getenv("ZERO_MYSQL_USER");
    const char* envPass = std::getenv("ZERO_MYSQL_PASS");
    const char* envDb = std::getenv("ZERO_MYSQL_DB");

    bool useRealMysql = (envUser != nullptr && envUser[0] != '\0');
    if (useRealMysql) {
        if (envHost) config.host = envHost;
        if (envDb) config.database = envDb;
        config.user = envUser;
        if (envPass) config.password = envPass;
        demo_mysql(config);
        return 0;
    }
#endif

    auto session = std::make_shared<DummySession>();
    session->addHook(std::make_shared<AuditLogger>());
    DbManager::instance()->registerSession("default", session);
    demo_query_builder(session);
    demo_repository(session);
    demo_advanced_model(session);
    demo_batch_and_page(session);
    demo_relation(session);
    demo_transaction(session);
    demo_validator(session);
    demo_hooks(session);
    demo_eager_load(session);
    demo_schema(session);
    std::cout << "\nDbManager stats: " << DbManager::instance()->getStatsJson() << std::endl;
    DbManager::instance()->closeAll();
    return 0;
}
