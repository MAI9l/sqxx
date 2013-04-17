
#include "sqlitepp.hpp"
#include "sqlitepp_detail.hpp"
#include <string>
#include <sqlite3.h>
#include <functional>
#include "callable.hpp"

namespace sqlitepp {

class value {
private:
	sqlite3_value *handle;
public:
	value(sqlite3_value *handle);

	bool null() const;
	operator int() const;
	operator int64_t() const;
	operator double() const;
	operator const char*() const;
	operator blob() const;

	sqlite3_value* raw();
};

bool value::null() const {
	return (sqlite3_value_type(handle) == SQLITE_NULL);
}

value::operator int() const {
	return sqlite3_value_int(handle);
}

value::operator int64_t() const {
	return sqlite3_value_int64(handle);
}

value::operator double() const {
	return sqlite3_value_double(handle);
}

value::operator const char*() const {
	const unsigned char *text = sqlite3_value_text(handle);
	return reinterpret_cast<const char*>(text);
}

value::operator blob() const {
	// Correct order to call functions according to http://www.sqlite.org/c3ref/column_blob.html
	const void *data = sqlite3_value_blob(handle);
	int len = sqlite3_value_bytes(handle);
	return std::make_pair(data, len);
}





/*
template<typename Ret, typename... Args>
Ret func_apply_array(Ret (*f)(Args...), void **argv) {
	return func_apply_array<0, Ret, Args...>::apply(f, argv);
}

template<typename Ret, typename... Args>
Ret func_apply_array(const std::function<Ret (Args...)> &fun, void **argv) {
	return func_apply_array<0, Ret, Args...>::apply(fun, argv);
}


template<int I, typename Ret, typename... PendingArgs>
struct sqxx_apply_array;

template<int I, typename Ret, typename Arg, typename... PendingArgs>
struct sqxx_apply_array<I, Ret, Arg, PendingArgs...> {
	template<typename Fun, typename... AppliedArgs>
	static Ret apply(Fun f, sqlite_value **argv, AppliedArgs... args) {
		return sqxx_apply_array<I+1, Ret, PendingArgs...>::template apply<Fun, AppliedArgs..., Arg>(f, argv, args..., value(argv[I]));
	}
};

template<int I, typename Ret>
struct sqxx_apply_array<I, Ret> {
	template<typename Fun, typename... AppliedArgs>
	static Ret apply(Fun f, void **argv, AppliedArgs... args) {
		return f(args...);
	}
};
*/

template<int I, int N>
struct sqxx_apply_array_n {
	template<typename Fun, typename... Values>
	static value apply(Fun f, sqlite3_value **argv, Values... args) {
		return sqxx_apply_array_n<I+1, N>::template apply<Fun, Values..., value>(std::forward(f), argv, std::forward(args)..., value(argv[I]));
	}
};

template<int I>
struct sqxx_apply_array_n<I, I> {
	template<typename Fun, typename... Values>
	static value apply(Fun f, void **argv, Values... args) {
		return value(f(args...));
	}
};


/*
template<int I, typename Ret, typename... PendingArgs>
struct sqxx_apply_array;

template<int I, typename Ret, typename Arg, typename... PendingArgs>
struct sqxx_apply_array<I, Ret, Arg, PendingArgs...> {
	template<typename Fun, typename... Values>
	static Ret apply(const Fun &f, sqlite3_value **argv, Values... args) {
		return sqxx_apply_array<I+1, Ret, PendingArgs...>::template apply<Fun, Values..., value>(f, argv, args..., value(argv[I]));
	}
};

template<int I, typename Ret>
struct sqxx_apply_array<I, Ret> {
	template<typename Fun, typename... Values>
	static Ret apply(const Fun &f, void **argv, Values... args) {
		return f(args...);
	}
};
*/

struct sqxx_fun_data {
	virtual ~sqxx_fun_data() {
	}
	virtual value call(int argc, sqlite3_value **argv) = 0;
};


template<int NArgs, typename Callable>
struct sqxx_fun_data_n : sqxx_fun_data {
	Callable fun;
	static const int nargs = callable_traits<Callable>::argc;

	sqxx_fun_data_n(Callable a_fun) : fun(std::forward(a_fun)) {
	}

	virtual value call(int argc, sqlite3_value **argv) {
		if (argc != NArgs) {
			throw error(SQLITE_MISUSE);
		}
		return sqxx_apply_array_n<NArgs, 0>::template apply<Callable>(fun, argv);
	}
};


extern "C"
void sqxx_fun_call(sqlite3_context *ctx, int argc, sqlite3_value** argv) {
	sqxx_fun_data *data = reinterpret_cast<sqxx_fun_data*>(sqlite3_user_data(ctx));
	try {
		data->call(argc, argv);
	}
	catch (const error &e) {
		sqlite3_result_error_code(ctx, e.code);
	}
	catch (const std::bad_alloc &) {
		sqlite3_result_error_nomem(ctx);
	}
	catch (const std::exception &e) {
		sqlite3_result_error(ctx, e.what(), -1);
	}
	catch (...) {
		sqlite3_result_error_code(ctx, SQLITE_MISUSE);
	}
}

extern "C"
void sqxx_fun_destroy(void *data) {
	sqxx_fun_data *d = reinterpret_cast<sqxx_fun_data*>(data);
	delete d;
}


static void create_function_p(sqlite3 *handle, const char *name, int nargs, sqxx_fun_data *fdat) {
	int rv;
	rv = sqlite3_create_function_v2(handle, name, nargs, SQLITE_UTF8, fdat,
			sqxx_fun_call, nullptr, nullptr, sqxx_fun_destroy);
	if (rv != SQLITE_OK) {
		delete fdat;
		throw static_error(rv);
	}
}

/*
template<typename Fun>
void connection::create_function(const char *name, const std::function<Fun> &f) {
	static const size_t NArgs = callable_traits<Fun>::argc;
	create_function_p(handle, name, NArgs, new sqxx_fun_data_n<Fun>(f));
}
*/

template<typename Callable>
void connection::create_function(const char *name, Callable f) {
	static const size_t NArgs = callable_traits<Callable>::argc;
	create_function_n<NArgs, Callable>(name, std::forward(f));
}

template<int NArgs, typename Callable>
void connection::create_function_n(const char *name, Callable f) {
	create_function_p(handle, name, NArgs, new sqxx_fun_data_n<NArgs, Callable>(std::forward(f)));
}

/*
template<typename Fun, int NArgs>
void create_function(const char *name, const Fun &fun) {
	scalar_data *data = new scalar_data_n<Fun, NArgs>(fun);
}
*/

/*
template<typename Fun>
void create_function(const char *name, const Fun &fun) {
	create_function<Fun, args_count<Fun>::value>(name, fun);
}

void create_function_varargs(const char *name, const Fun &fun) {
	scalar_data *data = new scalar_data_any<Fun>(fun);
}
*/

} // namepace sqlitepp
