// Thin SQLite wrapper: every call is serialized with a mutex (the web server is multi-threaded).
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <stdexcept>
#include "json.hpp"
#include "sqlite3.h"

using json = nlohmann::json;

class Db {
public:
    explicit Db(const std::string& path) {
        if (sqlite3_open(path.c_str(), &h_) != SQLITE_OK)
            throw std::runtime_error("Cannot open database: " + path);
        exec("PRAGMA foreign_keys=ON");
    }
    ~Db() { sqlite3_close(h_); }

    // SELECT -> JSON array of row objects. Parameters are bound as text (SQLite converts by column affinity).
    json query(const std::string& sql, const std::vector<std::string>& p = {}) {
        std::lock_guard<std::mutex> lk(m_);
        sqlite3_stmt* st = prepare(sql, p);
        json rows = json::array();
        int rc;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            json row = json::object();
            for (int i = 0; i < sqlite3_column_count(st); ++i) {
                const char* name = sqlite3_column_name(st, i);
                switch (sqlite3_column_type(st, i)) {
                    case SQLITE_INTEGER: row[name] = (long long)sqlite3_column_int64(st, i); break;
                    case SQLITE_FLOAT:   row[name] = sqlite3_column_double(st, i); break;
                    case SQLITE_NULL:    row[name] = nullptr; break;
                    default: row[name] = std::string((const char*)sqlite3_column_text(st, i));
                }
            }
            rows.push_back(row);
        }
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(h_));
        return rows;
    }

    // INSERT/UPDATE/DDL -> last inserted row id.
    long long exec(const std::string& sql, const std::vector<std::string>& p = {}) {
        std::lock_guard<std::mutex> lk(m_);
        sqlite3_stmt* st = prepare(sql, p);
        int rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(h_));
        return sqlite3_last_insert_rowid(h_);
    }

    int changes() { std::lock_guard<std::mutex> lk(m_); return sqlite3_changes(h_); }

private:
    sqlite3_stmt* prepare(const std::string& sql, const std::vector<std::string>& p) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(h_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
            throw std::runtime_error(std::string("SQL error: ") + sqlite3_errmsg(h_));
        for (size_t i = 0; i < p.size(); ++i)
            sqlite3_bind_text(st, (int)i + 1, p[i].c_str(), -1, SQLITE_TRANSIENT);
        return st;
    }
    sqlite3* h_ = nullptr;
    std::mutex m_;
};
