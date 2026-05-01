#pragma once

#include <sqlite3.h>
#include <string>

namespace fiddle {

class SqliteStatement {
public:
  SqliteStatement() = default;

  // Move-only semantics
  SqliteStatement(SqliteStatement &&other) noexcept : stmt_(other.stmt_) {
    other.stmt_ = nullptr;
  }
  SqliteStatement &operator=(SqliteStatement &&other) noexcept {
    if (this != &other) {
      finalize();
      stmt_ = other.stmt_;
      other.stmt_ = nullptr;
    }
    return *this;
  }

  SqliteStatement(const SqliteStatement &) = delete;
  SqliteStatement &operator=(const SqliteStatement &) = delete;

  ~SqliteStatement() { finalize(); }

  void finalize() {
    if (stmt_) {
      sqlite3_finalize(stmt_);
      stmt_ = nullptr;
    }
  }

  sqlite3_stmt *get() const { return stmt_; }
  bool isValid() const { return stmt_ != nullptr; }

  // Implicit conversion to raw pointer for easy sqlite3_* calls
  operator sqlite3_stmt *() const { return stmt_; }
  
  // Useful for out-ptr taking functions like sqlite3_prepare_v2
  sqlite3_stmt **outPtr() {
    finalize();
    return &stmt_;
  }

  void reset() {
    if (stmt_) {
      sqlite3_reset(stmt_);
      sqlite3_clear_bindings(stmt_);
    }
  }

private:
  sqlite3_stmt *stmt_ = nullptr;
};

} // namespace fiddle
