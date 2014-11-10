#pragma once

#include <memory>
#include <cstring>
#include <sqlite3.h>
#include <iod/sio.hh>
#include <iod/callable_traits.hh>

namespace iod
{

  void free_sqlite3_statement(void* s)
  {
    sqlite3_finalize((sqlite3_stmt*) s);
  }

  struct sqlite_statement
  {
    typedef std::shared_ptr<sqlite3_stmt> stmt_sptr;
    
    sqlite_statement(sqlite3_stmt* s) : stmt_(s),
                                   stmt_sptr_(stmt_sptr(s, free_sqlite3_statement))
    {
    }

    template <typename... A>
    void row_to_sio(iod::sio<A...>& o)
    {
      int ncols = sqlite3_column_count(stmt_);
      int filled[sizeof...(A)];
      memset(filled, 0, sizeof(filled));

      for (int i = 0; i < ncols; i++)
      {
        const char* cname = sqlite3_column_name(stmt_, i);
        bool found = false;
        foreach(o) | [&] (auto& m)
        {
          if (!found and !filled[i] and !strcmp(cname, m.symbol().name()))
          {
            this->read_column(i, m.value());
            found = true;
            filled[i] = 1;
          }
        };
      }
    }
  
    template <typename... A>
    int operator>>(iod::sio<A...>& o) {
      int code = sqlite3_step(stmt_);

      if (code != SQLITE_ROW)
        throw std::runtime_error("sqlite3_step did not return SQLITE_ROW.");

      row_to_sio(o);
    }

    template <typename F>
    void operator|(F f)
    {
      while (sqlite3_step(stmt_) == SQLITE_ROW)
      {
        typedef callable_arguments_tuple_t<F> tp;
        typedef std::remove_reference_t<std::tuple_element_t<0, tp>> T;
        T o;
        row_to_sio(o);
        f(o);
      }
    }

    template <typename V>
    void append_to(V& v)
    {
      (*this) | [&v] (typename V::value_type& o) { v.push_back(o); };
    }

    template <typename T>
    struct typed_iterator
    {
      template <typename F> void operator|(F f) const { (*_this) | [&f] (T& t) { f(t); }; }
      sqlite_statement* _this;
    };

    template <typename... A>
    auto operator()(A&&... typeinfo)
    {
      return typed_iterator<decltype(iod::D(typeinfo...))>{this};
    }

    void read_column(int pos, int& v) { v = sqlite3_column_int(stmt_, pos); }
    void read_column(int pos, float& v) { v = sqlite3_column_double(stmt_, pos); }
    void read_column(int pos, double& v) { v = sqlite3_column_double(stmt_, pos); }
    void read_column(int pos, int64_t& v) { v = sqlite3_column_int64(stmt_, pos); }
    void read_column(int pos, std::string& v) {
      auto str = sqlite3_column_text(stmt_, pos);
      auto n = sqlite3_column_bytes(stmt_, pos);
      v = std::string((const char*) str, n);
    }

    sqlite3_stmt* stmt_;
    stmt_sptr stmt_sptr_;
  };


  void free_sqlite3_db(void* db)
  {
    sqlite3_close_v2((sqlite3*) db);
  }

  struct sqlite_database
  {
    typedef std::shared_ptr<sqlite3> db_sptr;

    sqlite_database() : db_(nullptr)
    {
    }

    void connect(const std::string& filename, int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)
    {
      int r = sqlite3_open_v2(filename.c_str(), &db_, flags, nullptr);
      if (r != SQLITE_OK)
        throw std::runtime_error(std::string("Cannot open database ") + filename + " " + sqlite3_errstr(r));

      db_sptr_ = db_sptr(db_, free_sqlite3_db);
    }


    int bind(sqlite3_stmt* stmt, int pos, double d) const { return sqlite3_bind_double(stmt, pos, d); }
    int bind(sqlite3_stmt* stmt, int pos, int d) const { return sqlite3_bind_int(stmt, pos, d); }
    //void bind(sqlite3_stmt* stmt, int pos, null_t) { sqlite3_bind_null(stmt, pos); }
    int bind(sqlite3_stmt* stmt, int pos, const std::string& s) const {
      return sqlite3_bind_text(stmt, pos, s.data(), s.size(), nullptr); }
  
    template <typename... A>
    sqlite_statement operator()(const std::string& req, A&&... args) const
    {
      sqlite3_stmt* stmt;

      int err = sqlite3_prepare_v2(db_, req.c_str(), req.size(), &stmt, nullptr);
      if (err != SQLITE_OK)
        throw std::runtime_error(std::string("Sqlite error during prepare: ") + sqlite3_errstr(err));
  
      int i = 1;
      foreach(std::forward_as_tuple(args...)) | [&] (auto& m)
      {
        int err;
        if ((err = this->bind(stmt, i, m)) != SQLITE_OK)
          throw std::runtime_error(std::string("Sqlite error during binding: ") + sqlite3_errstr(err));
        i++;
      };

      return sqlite_statement(stmt);
    }
  
    sqlite3* db_;
    db_sptr db_sptr_;
  };

}
