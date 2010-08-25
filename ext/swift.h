#ifndef SWIFT_H
#define SWIFT_H

#include <dbic++.h>
#include <ruby/ruby.h>
#include <ruby/io.h>

#define CONST_GET(scope, constant) rb_funcall(scope, rb_intern("const_get"), 1, rb_str_new2(constant))
#define TO_S(v)                    rb_funcall(v, rb_intern("to_s"), 0)
#define CSTRING(v)                 RSTRING_PTR(TO_S(v))

extern VALUE cSwiftConnectionError;

#define CATCH_DBI_EXCEPTIONS() \
  catch (dbi::ConnectionError &error) { \
    rb_raise(cSwiftConnectionError, "%s", CSTRING(rb_str_new2(error.what()))); \
  } \
  catch (dbi::Error &error) { \
    rb_raise(cSwiftConnectionError, "%s", CSTRING(rb_str_new2(error.what()))); \
  }

#include "adapter.h"
#include "query.h"
#include "statement.h"
#include "iostream.h"

#endif

