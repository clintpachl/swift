#include <dbic++.h>
#include <ruby/ruby.h>
#include <ruby/io.h>
#include <time.h>
#include <unistd.h>

#define CONST_GET(scope, constant) rb_const_get(scope, rb_intern(constant))

static VALUE mSwift;
static VALUE cAdapter;
static VALUE cStatement;
static VALUE cResultSet;
static VALUE cPool;
static VALUE cRequest;
static VALUE cBigDecimal;
static VALUE cStringIO;

static VALUE eRuntimeError;
static VALUE eArgumentError;
static VALUE eStandardError;
static VALUE eConnectionError;

static VALUE fLoad;
static VALUE fStringify;
static VALUE fNew;
static VALUE fRead;
static VALUE fWrite;

size_t local_tzoffset;
char   errstr[8192];

#define CSTRING(v)    RSTRING_PTR(TYPE(v) == T_STRING ? v : rb_funcall(v, fStringify, 0))
#define OBJ2STRING(v) (TYPE(v) == T_STRING ? v : rb_funcall(v, fStringify, 0))

#define EXCEPTION(type) (dbi::ConnectionError &e) { \
    snprintf(errstr, 4096, "%s", e.what()); \
    rb_raise(eConnectionError, "%s : %s", type, errstr); \
} \
catch (dbi::Error &e) {\
    snprintf(errstr, 4096, "%s", e.what()); \
    rb_raise(eRuntimeError, "%s : %s", type, errstr); \
}

#define THREAD_BLOCKING_FUNCTION(f) ((VALUE (*)(void*)) f)

struct BlockingQuery {
    char *sql;
    dbi::Handle *handle;
    dbi::AbstractStatement *stmt;
    dbi::ResultRow bind;
};


class IOStream : public dbi::IOStream {
    private:
    string empty, data;
    VALUE stream;
    public:
    IOStream(VALUE s) {
        stream = s;
    }
    string& read() {
        VALUE response = rb_funcall(stream, fRead, 0);
        if (response == Qnil)
            return empty;
        else {
            if (TYPE(response) != T_STRING)
                rb_raise(eArgumentError,
                    "Adapter#write can only process string data. You need to stringify values returned in the callback.");
            data = string(RSTRING_PTR(response), RSTRING_LEN(response));
            return data;
        }
    }

    uint read(char *buffer, uint len) {
        VALUE response = rb_funcall(stream, fRead, 1, INT2NUM(len));
        if (response == Qnil)
            return 0;
        else {
            len = len < RSTRING_LEN(response) ? len : RSTRING_LEN(response);
            memcpy(buffer, RSTRING_PTR(response), len);
            return len;
        }
    }

    void write(const char *str) {
        rb_funcall(stream, fWrite, 1, rb_str_new2(str));
    }
    void write(const char *str, ulong l) {
        rb_funcall(stream, fWrite, 1, rb_str_new(str, l));
    }
    void truncate() {
        data = "";
    }
};


static dbi::Handle* DBI_HANDLE(VALUE self) {
    dbi::Handle *h;
    Data_Get_Struct(self, dbi::Handle, h);
    if (!h) rb_raise(eRuntimeError, "Invalid object, did you forget to call #super ?");
    return h;
}

static dbi::AbstractStatement* DBI_STATEMENT(VALUE self) {
    dbi::AbstractStatement *st;
    Data_Get_Struct(self, dbi::AbstractStatement, st);
    if (!st) rb_raise(eRuntimeError, "Invalid object, did you forget to call #super ?");
    return st;
}

static dbi::ConnectionPool* DBI_CPOOL(VALUE self) {
    dbi::ConnectionPool *cp;
    Data_Get_Struct(self, dbi::ConnectionPool, cp);
    if (!cp) rb_raise(eRuntimeError, "Invalid object, did you forget to call #super ?");
    return cp;
}

static dbi::Request* DBI_REQUEST(VALUE self) {
    dbi::Request *r;
    Data_Get_Struct(self, dbi::Request, r);
    if (!r) rb_raise(eRuntimeError, "Invalid object, did you forget to call #super ?");
    return r;
}

void static inline rb_extract_bind_params(int argc, VALUE* argv, std::vector<dbi::Param> &bind) {
    for (int i = 0; i < argc; i++) {
        VALUE arg = argv[i];
        if (arg == Qnil)
            bind.push_back(dbi::PARAM(dbi::null()));
        else if (rb_obj_is_kind_of(arg, rb_cIO) ==  Qtrue || rb_obj_is_kind_of(arg, cStringIO) ==  Qtrue) {
            arg = rb_funcall(arg, fRead, 0);
            bind.push_back(dbi::PARAM_BINARY((unsigned char*)RSTRING_PTR(arg), RSTRING_LEN(arg)));
        }
        else {
            arg = OBJ2STRING(arg);
            if (strcmp(rb_enc_get(arg)->name, "UTF-8") != 0)
                arg = rb_str_encode(arg, rb_str_new2("UTF-8"), 0, Qnil);
            bind.push_back(dbi::PARAM((unsigned char*)RSTRING_PTR(arg), RSTRING_LEN(arg)));
        }
    }
}

VALUE rb_swift_init(VALUE self, VALUE path) {
    try { dbi::dbiInitialize(CSTRING(path)); } catch EXCEPTION("Swift#init");
    return Qtrue;
}

static void free_connection(dbi::Handle *self) {
    if (self) delete self;
}

VALUE rb_adapter_alloc(VALUE klass) {
    dbi::Handle *h = 0;
    return Data_Wrap_Struct(klass, 0, free_connection, h);
}

VALUE rb_adapter_init(VALUE self, VALUE opts) {
    VALUE db       = rb_hash_aref(opts, ID2SYM(rb_intern("db")));
    VALUE host     = rb_hash_aref(opts, ID2SYM(rb_intern("host")));
    VALUE port     = rb_hash_aref(opts, ID2SYM(rb_intern("port")));
    VALUE user     = rb_hash_aref(opts, ID2SYM(rb_intern("user")));
    VALUE driver   = rb_hash_aref(opts, ID2SYM(rb_intern("driver")));
    VALUE password = rb_hash_aref(opts, ID2SYM(rb_intern("password")));
    VALUE zone     = rb_hash_aref(opts, ID2SYM(rb_intern("timezone")));

    if (NIL_P(db)) rb_raise(eArgumentError, "Adapter#new called without :db");
    if (NIL_P(driver)) rb_raise(eArgumentError, "Adapter#new called without :driver");

    host     = NIL_P(host)     ? rb_str_new2("") : host;
    port     = NIL_P(port)     ? rb_str_new2("") : port;
    user     = NIL_P(user)     ? rb_str_new2(getlogin()) : user;
    password = NIL_P(password) ? rb_str_new2("") : password;

    try {
        DATA_PTR(self) = new dbi::Handle(
            CSTRING(driver), CSTRING(user), CSTRING(password),
            CSTRING(db), CSTRING(host), CSTRING(port)
        );
    } catch EXCEPTION("Adapter#new");

    // NOTE: We need the options ivar - Swift#pool uses it to setup pools based on
    //       names used in Swift#setup.
    rb_iv_set(self, "@options", opts);
    rb_iv_set(self, "@timezone", zone);
    return Qnil;
}

VALUE rb_adapter_close(VALUE self) {
    dbi::Handle *h = DBI_HANDLE(self);
    try {
        h->close();
    } catch EXCEPTION("Adapter#close");
    return Qtrue;
}

static void free_statement(dbi::AbstractStatement *self) {
    if (self) {
        self->cleanup();
        delete self;
    }
}

static VALUE rb_adapter_prepare(int argc, VALUE *argv, VALUE self) {
    VALUE sql, scheme, prepared;
    dbi::Handle *h = DBI_HANDLE(self);

    rb_scan_args(argc, argv, "11", &scheme, &sql);
    if (TYPE(scheme) != T_CLASS) {
        sql    = scheme;
        scheme = Qnil;
    }

    try {
        dbi::AbstractStatement *st = h->conn()->prepare(CSTRING(sql));
        prepared = Data_Wrap_Struct(cStatement, 0, free_statement, st);
        rb_iv_set(prepared, "@scheme",   scheme);
        rb_iv_set(prepared, "@timezone", rb_iv_get(self, "@timezone"));
    } catch EXCEPTION("Adapter#prepare");

    return prepared;
}

static VALUE rb_statement_each(VALUE self);
VALUE rb_statement_execute(int argc, VALUE *argv, VALUE self);

VALUE handle_execute(BlockingQuery *q) {
    return UINT2NUM(q->handle->conn()->execute(q->sql));
}

VALUE handle_execute_bind(BlockingQuery *q) {
    return UINT2NUM(q->handle->conn()->execute(q->sql, q->bind));
}

VALUE rb_adapter_execute(int argc, VALUE *argv, VALUE self) {
    VALUE rows;
    VALUE result = 0;
    dbi::Handle *h = DBI_HANDLE(self);

    if (argc == 0 || NIL_P(argv[0]))
        rb_raise(eArgumentError, "Adapter#execute called without a SQL command");

    try {
        BlockingQuery query;
        query.sql    = CSTRING(argv[0]);
        query.handle = h;
        if (argc == 1) {
            if (dbi::_trace)
                dbi::logMessage(dbi::_trace_fd, query.sql);
            rows = rb_thread_blocking_region(THREAD_BLOCKING_FUNCTION(handle_execute), &query, 0, 0);
        }
        else {
            rb_extract_bind_params(argc-1, argv+1, query.bind);
            if (dbi::_trace)
                dbi::logMessage(dbi::_trace_fd, dbi::formatParams(query.sql, query.bind));
            rows = rb_thread_blocking_region(THREAD_BLOCKING_FUNCTION(handle_execute_bind), &query, 0, 0);
        }
        if (rb_block_given_p()) {
            dbi::AbstractResultSet *rs = h->results();
            result = Data_Wrap_Struct(cResultSet, 0, free_statement, rs);
            rb_iv_set(result, "@timezone", rb_iv_get(self, "@timezone"));
        }
    } catch EXCEPTION("Adapter#execute");

    return result ? rb_statement_each(result) : rows;
}

VALUE rb_adapter_results(VALUE self) {
    VALUE result = Qnil;
    dbi::Handle *h = DBI_HANDLE(self);
    try {
        dbi::AbstractResultSet *rs = h->results();
        result = Data_Wrap_Struct(cResultSet, 0, free_statement, rs);
        rb_iv_set(result, "@timezone", rb_iv_get(self, "@timezone"));
    } catch EXCEPTION("Adapter#results");
    return result;
}

VALUE rb_adapter_begin(int argc, VALUE *argv, VALUE self) {
    dbi::Handle *h = DBI_HANDLE(self);
    VALUE save;
    rb_scan_args(argc, argv, "01", &save);
    try { NIL_P(save) ? h->begin() : h->begin(CSTRING(save)); } catch EXCEPTION("Adapter#begin");
}

VALUE rb_adapter_commit(int argc, VALUE *argv, VALUE self) {
    dbi::Handle *h = DBI_HANDLE(self);
    VALUE save;
    rb_scan_args(argc, argv, "01", &save);
    try { NIL_P(save) ? h->commit() : h->commit(CSTRING(save)); } catch EXCEPTION("Adapter#commit");
}

VALUE rb_adapter_rollback(int argc, VALUE *argv, VALUE self) {
    dbi::Handle *h = DBI_HANDLE(self);
    VALUE save_point;
    rb_scan_args(argc, argv, "01", &save_point);
    try { NIL_P(save_point) ? h->rollback() : h->rollback(CSTRING(save_point)); } catch EXCEPTION("Adapter#rollback");
}

VALUE rb_adapter_transaction(int argc, VALUE *argv, VALUE self) {
    int status;
    VALUE sp, block;

    dbi::Handle *h  = DBI_HANDLE(self);

    rb_scan_args(argc, argv, "01&", &sp, &block);
    if (NIL_P(block))
        rb_raise(eArgumentError, "Adapter#transaction{} called without a block");

    std::string save_point = NIL_P(sp) ? "SP" + dbi::generateCompactUUID() : CSTRING(sp);

    try {
        h->begin(save_point);
        rb_protect(rb_yield, self, &status);
        if (!status && h->transactions().size() > 0) {
            h->commit(save_point);
        }
        else if (status && h->transactions().size() > 0) {
            h->rollback(save_point);
            rb_jump_tag(status);
        }
    } catch EXCEPTION("Adapter#transaction{}");
    return Qtrue;
}

VALUE rb_adapter_write(int argc, VALUE *argv, VALUE self) {
    ulong rows = 0;
    VALUE stream, table, fields;

    rb_scan_args(argc, argv, "30", &table, &fields, &stream);
    if (TYPE(stream) != T_STRING && !rb_respond_to(stream, fRead))
        rb_raise(eArgumentError, "Adapter#write: stream should be a string or kind_of?(IO)");
    if (TYPE(fields) != T_ARRAY)
        rb_raise(eArgumentError, "Adapter#write: fields should be an array of string values");

    dbi::Handle *h = DBI_HANDLE(self);
    try {
        dbi::FieldSet rfields;
        for (int n = 0; n < RARRAY_LEN(fields); n++) {
            VALUE f = rb_ary_entry(fields, n);
            rfields << std::string(RSTRING_PTR(f), RSTRING_LEN(f));
        }
        // This is just for the friggin mysql support - mysql does not like a statement close
        // command being send on a handle when the writing has started.
        rb_gc();
        if (TYPE(stream) == T_STRING) {
            dbi::IOStream io(RSTRING_PTR(stream), RSTRING_LEN(stream));
            rows = h->write(RSTRING_PTR(table), rfields, &io);
        }
        else {
            IOStream io(stream);
            rows = h->write(RSTRING_PTR(table), rfields, &io);
        }
    } catch EXCEPTION("Adapter#write");

    return ULONG2NUM(rows);
}

int compute_local_tzoffset() {
    tzset();
    return -1 * timezone;
}

ulong rb_zone_to_offset(VALUE zone) {
    ulong offset;
    char buffer[512];
    char *old, saved[512];

    if NIL_P(zone) return 0;

    // save current zone setting.
    if ((old = getenv("TZ"))) strcpy(saved, old);

    snprintf(buffer, 512, ":%s", CSTRING(zone));
    setenv("TZ", buffer, 1);
    tzset();
    offset = timezone;

    // reset it back, no need to use rb_ensure coz we can't get any VM errors here.
    if (old)
        setenv("TZ", saved, 1);
    else
        unsetenv("TZ");

    return -1 * offset;
}

VALUE rb_adapter_escape(VALUE self, VALUE val) {
    VALUE escaped = Qnil;
    if (TYPE(val) != T_STRING) rb_raise(eArgumentError, "Adapter#escape: Cannot escape non-string value");

    dbi::Handle *h = DBI_HANDLE(self);
    try {
        std::string safe = h->escape(std::string(RSTRING_PTR(val), RSTRING_LEN(val)));
        escaped = rb_str_new(safe.data(), safe.length());
    } catch EXCEPTION("Adapter#escape");
    return escaped;
}

VALUE rb_statement_alloc(VALUE klass) {
    dbi::AbstractStatement *st = 0;
    return Data_Wrap_Struct(klass, 0, free_statement, st);
}

VALUE rb_statement_init(VALUE self, VALUE hl, VALUE sql) {
    dbi::Handle *h = DBI_HANDLE(hl);

    if (NIL_P(hl) || !h)
        rb_raise(eArgumentError, "Statement#new called without an Adapter instance");
    if (NIL_P(sql))
        rb_raise(eArgumentError, "Statement#new called without a SQL command");

    try {
        DATA_PTR(self) = h->conn()->prepare(CSTRING(sql));
    } catch EXCEPTION("Statement#new");

    return Qnil;
}

VALUE statement_execute(BlockingQuery *q) {
    q->stmt->execute();
    return Qnil;
}

VALUE statement_execute_bind(BlockingQuery *q) {
    q->stmt->execute(q->bind);
    return Qnil;
}

VALUE rb_statement_execute(int argc, VALUE *argv, VALUE self) {
    dbi::AbstractStatement *st = DBI_STATEMENT(self);
    try {
        BlockingQuery query;
        query.stmt = st;
        if (argc == 0) {
            dbi::ResultRow params;
            if (dbi::_trace)
                dbi::logMessage(dbi::_trace_fd, dbi::formatParams(st->command(), params));
            rb_thread_blocking_region(THREAD_BLOCKING_FUNCTION(statement_execute), &query, 0, 0);
        }
        else {
            rb_extract_bind_params(argc, argv, query.bind);
            if (dbi::_trace)
                dbi::logMessage(dbi::_trace_fd, dbi::formatParams(st->command(), query.bind));
            rb_thread_blocking_region(THREAD_BLOCKING_FUNCTION(statement_execute_bind), &query, 0, 0);
        }
    } catch EXCEPTION("Statement#execute");

    if (rb_block_given_p()) return rb_statement_each(self);
    return self;
}

VALUE rb_statement_finish(VALUE self) {
    dbi::AbstractStatement *st = DBI_STATEMENT(self);
    try {
        st->finish();
    } catch EXCEPTION("Statement#finish");
}

VALUE rb_statement_rows(VALUE self) {
    uint rows;
    dbi::AbstractStatement *st = DBI_STATEMENT(self);
    try { rows = st->rows(); } catch EXCEPTION("Statement#rows");
    return INT2NUM(rows);
}

VALUE rb_statement_insert_id(VALUE self) {
  dbi::AbstractStatement *st = DBI_STATEMENT(self);
  VALUE insert_id    = Qnil;
  try {
    if (st->rows() > 0) insert_id = LONG2NUM(st->lastInsertID());
  } catch EXCEPTION("Statement#insert_id");

  return insert_id;
}

VALUE rb_field_typecast(int type, const char *data, ulong len, ulong adapter_tzoffset) {
    time_t epoch, offset;
    struct tm tm;

    double usec = 0;
    char tzsign = 0;
    int tzhour  = 0, tzmin = 0;

    switch(type) {
        case DBI_TYPE_BOOLEAN:
            return strcmp(data, "t") == 0 || strcmp(data, "1") == 0 ? Qtrue : Qfalse;
        case DBI_TYPE_INT:
            return rb_cstr2inum(data, 10);
        case DBI_TYPE_BLOB:
            return rb_funcall(cStringIO, fNew, 1, rb_str_new(data, len));
        // forcing UTF8 convention here - do we really care about people using non utf8
        // client encodings and databases ?
        case DBI_TYPE_TEXT:
            return rb_enc_str_new(data, len, rb_utf8_encoding());
        case DBI_TYPE_TIME:
            /* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
               NOTE Flexibility sacrificed for performance.
                    Timestamp parser is very unforgiving and only parses
                    YYYY-MM-DD HH:MM:SS.ms[+-]HH:MM
            ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
            memset(&tm, 0, sizeof(struct tm));
            if (strchr(data, '.')) {
                sscanf(data, "%04d-%02d-%02d %02d:%02d:%02d%lf%c%02d:%02d",
                    &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec,
                    &usec, &tzsign, &tzhour, &tzmin);
            }
            else {
                sscanf(data, "%04d-%02d-%02d %02d:%02d:%02d%c%02d:%02d",
                    &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec,
                    &tzsign, &tzhour, &tzmin);
            }
            tm.tm_year -= 1900;
            tm.tm_mon  -= 1;
            if (tm.tm_mday > 0) {
                offset = local_tzoffset;
                epoch  = mktime(&tm);
                if (tzsign == '+' || tzsign == '-') {
                    offset += tzsign == '+' ?
                          (time_t)tzhour * -3600 + (time_t)tzmin * -60
                        : (time_t)tzhour *  3600 + (time_t)tzmin *  60;
                }
                else offset -= adapter_tzoffset;
                return rb_time_new(epoch + offset, usec*1000000);
            }
            else {
                fprintf(stderr, "typecast failed to parse date: %s\n", data);
                return rb_str_new(data, len);
            }
        // does bigdecimal solve all floating point woes ? dunno :)
        case DBI_TYPE_NUMERIC:
            return rb_funcall(cBigDecimal, fNew, 1, rb_str_new2(data));
        case DBI_TYPE_FLOAT:
            return rb_float_new(atof(data));
    }
}

static VALUE rb_statement_each(VALUE self) {
    uint r, c;
    ulong len, adapter_tzoffset;
    const char *data;

    dbi::AbstractStatement *st = DBI_STATEMENT(self);
    VALUE scheme     = rb_iv_get(self, "@scheme");
    VALUE zone       = rb_iv_get(self, "@timezone");
    adapter_tzoffset = rb_zone_to_offset(zone);
    local_tzoffset   = compute_local_tzoffset();

    try {
        VALUE attrs = rb_ary_new();
        std::vector<string> fields = st->fields();
        std::vector<int> types     = st->types();
        for (c = 0; c < fields.size(); c++) {
            rb_ary_push(attrs, ID2SYM(rb_intern(fields[c].c_str())));
        }

        // TODO Code duplication
        //      Avoiding a rb_yield(NIL_P(scheme) ? row : rb_funcall(scheme, fLoad, 1, row))
        st->seek(0);
        if (NIL_P(scheme)) {
            for (r = 0; r < st->rows(); r++) {
                VALUE row = rb_hash_new();
                for (c = 0; c < st->columns(); c++) {
                    data = (const char*)st->read(r,c, &len);
                    if (data)
                        rb_hash_aset(row, rb_ary_entry(attrs, c),
                            rb_field_typecast(types[c], data, len, adapter_tzoffset));
                    else
                        rb_hash_aset(row, rb_ary_entry(attrs, c), Qnil);
                }
                rb_yield(row);
            }
        }
        else {
            for (r = 0; r < st->rows(); r++) {
                VALUE row = rb_hash_new();
                for (c = 0; c < st->columns(); c++) {
                    data = (const char*)st->read(r,c, &len);
                    if (data)
                        rb_hash_aset(row, rb_ary_entry(attrs, c),
                            rb_field_typecast(types[c], data, len, adapter_tzoffset));
                    else
                        rb_hash_aset(row, rb_ary_entry(attrs, c), Qnil);
                }
                rb_yield(rb_funcall(scheme, fLoad, 1, row));
            }
        }
    } catch EXCEPTION("Statment#each");
    return Qnil;
}

VALUE rb_statement_read(VALUE self) {
    const char *data;
    uint r, c;
    ulong len;
    VALUE row = Qnil;
    dbi::AbstractStatement *st = DBI_STATEMENT(self);
    try {
        r = st->tell();
        if (r < st->rows()) {
            row = rb_ary_new();
            for (c = 0; c < st->columns(); c++) {
                data = (const char*)st->read(r, c, &len);
                rb_ary_push(row, data ? rb_str_new(data, len) : Qnil);
            }
        }
    } catch EXCEPTION("Statement#read");

    return row;
}

VALUE rb_statement_rewind(VALUE self) {
    dbi::AbstractStatement *st = DBI_STATEMENT(self);
    try { st->rewind(); } catch EXCEPTION("Statement#rewind");
    return Qnil;
}

VALUE rb_swift_trace(int argc, VALUE *argv, VALUE self) {
    // by default log all messages to stderr.
    int fd = 2;
    rb_io_t *fptr;
    VALUE flag, io;

    rb_scan_args(argc, argv, "11", &flag, &io);

    if (TYPE(flag) != T_TRUE && TYPE(flag) != T_FALSE)
        rb_raise(eArgumentError, "Swift#trace expects a boolean flag, got %s", CSTRING(flag));

    if (!NIL_P(io)) {
        GetOpenFile(rb_convert_type(io, T_FILE, "IO", "to_io"), fptr);
        fd = fptr->fd;
    }

    dbi::trace(flag == Qtrue ? true : false, fd);
}

VALUE rb_adapter_dup(VALUE self) {
    rb_raise(eRuntimeError, "Adapter#dup or Adapter#clone is not allowed.");
}

VALUE rb_statement_dup(VALUE self) {
    rb_raise(eRuntimeError, "Statement#dup or Statement#clone is not allowed.");
}

static void free_request(dbi::Request *self) {
    if(self) delete self;
}

VALUE rb_request_alloc(VALUE klass) {
    dbi::Request *r = 0;
    return Data_Wrap_Struct(klass, 0, free_request, r);
}

static void free_cpool(dbi::ConnectionPool *self) {
    if (self) delete self;
}

VALUE rb_cpool_alloc(VALUE klass) {
    dbi::ConnectionPool *c = 0;
    return Data_Wrap_Struct(klass, 0, free_cpool, c);
}

VALUE rb_cpool_init(VALUE self, VALUE n, VALUE opts) {
    VALUE db       = rb_hash_aref(opts, ID2SYM(rb_intern("db")));
    VALUE host     = rb_hash_aref(opts, ID2SYM(rb_intern("host")));
    VALUE port     = rb_hash_aref(opts, ID2SYM(rb_intern("port")));
    VALUE user     = rb_hash_aref(opts, ID2SYM(rb_intern("user")));
    VALUE driver   = rb_hash_aref(opts, ID2SYM(rb_intern("driver")));
    VALUE password = rb_hash_aref(opts, ID2SYM(rb_intern("password")));
    VALUE zone     = rb_hash_aref(opts, ID2SYM(rb_intern("timezone")));

    if (NIL_P(db)) rb_raise(eArgumentError, "ConnectionPool#new called without :db");
    if (NIL_P(driver)) rb_raise(eArgumentError, "ConnectionPool#new called without :driver");

    host     = NIL_P(host)     ? rb_str_new2("") : host;
    port     = NIL_P(port)     ? rb_str_new2("") : port;
    user     = NIL_P(user)     ? rb_str_new2(getlogin()) : user;
    password = NIL_P(password) ? rb_str_new2("") : password;

    if (NUM2INT(n) < 1) rb_raise(eArgumentError, "ConnectionPool#new called with invalid pool size.");

    try {
        DATA_PTR(self) = new dbi::ConnectionPool(
            NUM2INT(n), CSTRING(driver), CSTRING(user), CSTRING(password), CSTRING(db), CSTRING(host), CSTRING(port)
        );
    } catch EXCEPTION("ConnectionPool#new");

    rb_iv_set(self, "@timezone", zone);
    return Qnil;
}

void rb_cpool_callback(dbi::AbstractResultSet *rs) {
    VALUE callback = (VALUE)rs->context;
    // NOTE: ResultSet will be free'd by the underlying connection pool dispatcher.
    if (!NIL_P(callback)) {
        VALUE result = Data_Wrap_Struct(cResultSet, 0, 0, rs);
        rb_iv_set(result, "@timezone", rb_iv_get(callback, "@timezone"));
        rb_proc_call(callback, rb_ary_new3(1, result));
    }
}

VALUE rb_cpool_execute(int argc, VALUE *argv, VALUE self) {
    dbi::ConnectionPool *cp = DBI_CPOOL(self);
    int n;
    VALUE sql;
    VALUE args;
    VALUE callback;
    VALUE request = Qnil;

    rb_scan_args(argc, argv, "1*&", &sql, &args, &callback);

    if (NIL_P(callback))
        rb_raise(eArgumentError, "No block given in Pool#execute");

    // HACK: cheating a bit, we need the timezone when creating the resultset.
    rb_iv_set(callback, "@timezone", rb_iv_get(self, "@timezone"));

    try {
        dbi::ResultRow bind;
        for (n = 0; n < RARRAY_LEN(args); n++) {
            VALUE arg = rb_ary_entry(args, n);
            if (arg == Qnil)
                bind.push_back(dbi::PARAM(dbi::null()));
            else if (rb_obj_is_kind_of(arg, rb_cIO) ==  Qtrue || rb_obj_is_kind_of(arg, cStringIO) ==  Qtrue) {
                arg = rb_funcall(arg, fRead, 0);
                bind.push_back(dbi::PARAM_BINARY((unsigned char*)RSTRING_PTR(arg), RSTRING_LEN(arg)));
            }
            else {
                arg = OBJ2STRING(arg);
                if (strcmp(rb_enc_get(arg)->name, "UTF-8") != 0)
                    arg = rb_str_encode(arg, rb_str_new2("UTF-8"), 0, Qnil);
                bind.push_back(dbi::PARAM((unsigned char*)RSTRING_PTR(arg), RSTRING_LEN(arg)));
            }
        }
        // TODO GC mark callback.
        request = rb_request_alloc(cRequest);
        DATA_PTR(request) = cp->execute(CSTRING(sql), bind, rb_cpool_callback, (void*)callback);
    } catch EXCEPTION("ConnectionPool#execute");

    return DATA_PTR(request) ? request : Qnil;
}

VALUE rb_request_socket(VALUE self) {
    dbi::Request *r = DBI_REQUEST(self);
    VALUE fd = Qnil;
    try {
        fd = INT2NUM(r->socket());
    } catch EXCEPTION("Request#socket");
    return fd;
}

VALUE rb_request_process(VALUE self) {
    VALUE rc = Qnil;
    dbi::Request *r = DBI_REQUEST(self);

    try {
        rc = r->process() ? Qtrue : Qfalse;
    } catch EXCEPTION("Request#process");

    return rc;
}

VALUE rb_special_constant(VALUE self, VALUE obj) {
    return rb_special_const_p(obj) ? Qtrue : Qfalse;
}

extern "C" {
    void Init_swift(void) {
        rb_require("bigdecimal");
        rb_require("stringio");

        fNew             = rb_intern("new");
        fStringify       = rb_intern("to_s");
        fLoad            = rb_intern("load");
        fRead            = rb_intern("read");
        fWrite           = rb_intern("write");

        eRuntimeError    = CONST_GET(rb_mKernel, "RuntimeError");
        eArgumentError   = CONST_GET(rb_mKernel, "ArgumentError");
        eStandardError   = CONST_GET(rb_mKernel, "StandardError");
        cBigDecimal      = CONST_GET(rb_mKernel, "BigDecimal");
        cStringIO        = CONST_GET(rb_mKernel, "StringIO");
        eConnectionError = rb_define_class("ConnectionError", eRuntimeError);

        mSwift           = rb_define_module("Swift");
        cAdapter         = rb_define_class_under(mSwift, "Adapter", rb_cObject);
        cStatement       = rb_define_class_under(mSwift, "Statement", rb_cObject);
        cResultSet       = rb_define_class_under(mSwift, "ResultSet", cStatement);
        cPool            = rb_define_class_under(mSwift, "ConnectionPool", rb_cObject);
        cRequest         = rb_define_class_under(mSwift, "Request", rb_cObject);

        rb_define_module_function(mSwift, "init",  RUBY_METHOD_FUNC(rb_swift_init), 1);
        rb_define_module_function(mSwift, "trace", RUBY_METHOD_FUNC(rb_swift_trace), -1);
        rb_define_module_function(mSwift, "special_constant?", RUBY_METHOD_FUNC(rb_special_constant), 1);

        rb_define_alloc_func(cAdapter, rb_adapter_alloc);

        rb_define_method(cAdapter, "initialize",  RUBY_METHOD_FUNC(rb_adapter_init), 1);
        rb_define_method(cAdapter, "prepare",     RUBY_METHOD_FUNC(rb_adapter_prepare), -1);
        rb_define_method(cAdapter, "execute",     RUBY_METHOD_FUNC(rb_adapter_execute), -1);
        rb_define_method(cAdapter, "begin",       RUBY_METHOD_FUNC(rb_adapter_begin), -1);
        rb_define_method(cAdapter, "commit",      RUBY_METHOD_FUNC(rb_adapter_commit), -1);
        rb_define_method(cAdapter, "rollback",    RUBY_METHOD_FUNC(rb_adapter_rollback), -1);
        rb_define_method(cAdapter, "transaction", RUBY_METHOD_FUNC(rb_adapter_transaction), -1);
        rb_define_method(cAdapter, "close",       RUBY_METHOD_FUNC(rb_adapter_close), 0);
        rb_define_method(cAdapter, "dup",         RUBY_METHOD_FUNC(rb_adapter_dup), 0);
        rb_define_method(cAdapter, "clone",       RUBY_METHOD_FUNC(rb_adapter_dup), 0);
        rb_define_method(cAdapter, "write",       RUBY_METHOD_FUNC(rb_adapter_write), -1);
        rb_define_method(cAdapter, "results",     RUBY_METHOD_FUNC(rb_adapter_results), 0);
        rb_define_method(cAdapter, "escape",      RUBY_METHOD_FUNC(rb_adapter_escape), 1);

        rb_define_alloc_func(cStatement, rb_statement_alloc);

        rb_define_method(cStatement, "initialize",  RUBY_METHOD_FUNC(rb_statement_init), 2);
        rb_define_method(cStatement, "execute",     RUBY_METHOD_FUNC(rb_statement_execute), -1);
        rb_define_method(cStatement, "each",        RUBY_METHOD_FUNC(rb_statement_each), 0);
        rb_define_method(cStatement, "rows",        RUBY_METHOD_FUNC(rb_statement_rows), 0);
        rb_define_method(cStatement, "read",        RUBY_METHOD_FUNC(rb_statement_read), 0);
        rb_define_method(cStatement, "finish",      RUBY_METHOD_FUNC(rb_statement_finish), 0);
        rb_define_method(cStatement, "dup",         RUBY_METHOD_FUNC(rb_statement_dup), 0);
        rb_define_method(cStatement, "clone",       RUBY_METHOD_FUNC(rb_statement_dup), 0);
        rb_define_method(cStatement, "insert_id",   RUBY_METHOD_FUNC(rb_statement_insert_id), 0);
        rb_define_method(cStatement, "rewind",      RUBY_METHOD_FUNC(rb_statement_rewind), 0);

        rb_include_module(cStatement, CONST_GET(rb_mKernel, "Enumerable"));


        rb_define_alloc_func(cPool, rb_cpool_alloc);

        rb_define_method(cPool, "initialize",  RUBY_METHOD_FUNC(rb_cpool_init), 2);
        rb_define_method(cPool, "execute",     RUBY_METHOD_FUNC(rb_cpool_execute), -1);

        rb_define_alloc_func(cRequest, rb_request_alloc);

        rb_define_method(cRequest, "socket",      RUBY_METHOD_FUNC(rb_request_socket), 0);
        rb_define_method(cRequest, "process",     RUBY_METHOD_FUNC(rb_request_process), 0);

        rb_define_method(cResultSet, "execute", RUBY_METHOD_FUNC(Qnil), 0);
    }
}
