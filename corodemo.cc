/*
g++ -fcoroutines -std=c++20 -Wall -Werror corodemo.cc
clang++ -std=c++20 -stdlib=libc++ -fcoroutines-ts -Wall -Werror corodemo.cc
*/

#include <concepts>
/*#include <coroutine>*/
#include<experimental/coroutine>
#include <exception>
#include <iostream>
using namespace std;
using namespace experimental;
struct ReturnObject {
  struct promise_type {
    ReturnObject get_return_object() { return {}; }
    suspend_never initial_suspend() { return {}; }
    suspend_never final_suspend() { return {}; }
    void unhandled_exception() {}
    void return_void() noexcept {}
  };
};

struct Awaiter {
  coroutine_handle<>* hp_;
  constexpr bool await_ready() const noexcept { return false; }
  void await_suspend(coroutine_handle<> h) { 
    *hp_ = h; 
  }
  constexpr void await_resume() const noexcept {}
};

ReturnObject
counter(coroutine_handle<>* continuation_out)
{
  Awaiter a{ continuation_out };
  for (unsigned i = 0;; ++i) {
    co_await a;
    cout << "counter: " << i << endl;
  }
}

void
main1()
{
  coroutine_handle<> h;
  counter(&h);
  for (int i = 0; i < 3; ++i) {
    cout << "In main1 function\n";
    h();
  }
  h.destroy();
}

struct ReturnObject2 {
  struct promise_type {
    ReturnObject2 get_return_object() {
      return {
        // Uses C++20 designated initializer
        .h_ = coroutine_handle<promise_type>::from_promise(*this)
      };
    }
    suspend_never initial_suspend() { return {}; }
    suspend_never final_suspend() { return {}; }
    void unhandled_exception() {}
    void return_void() noexcept {}
  };

  coroutine_handle<promise_type> h_;
  operator coroutine_handle<promise_type>() const { return h_; }
};

ReturnObject2
counter2()
{
  for (unsigned i = 0;; ++i) {
    co_await suspend_always{};
    cout << "counter2: " << i << endl;
  }
}

void
main2()
{
  // A coroutine_handle<promise_type> converts to coroutine_handle<>
  coroutine_handle<> h = counter2();
  for (int i = 0; i < 3; ++i) {
    cout << "In main2 function\n";
    h();
  }
  h.destroy();
}

struct ReturnObject3 {
  struct promise_type {
    unsigned value_;

    ReturnObject3 get_return_object() {
      return ReturnObject3{
        .h_ = coroutine_handle<promise_type>::from_promise(*this)
      };
    }
    suspend_never initial_suspend() { return {}; }
    suspend_never final_suspend() { return {}; }
    void unhandled_exception() {}
    void return_void() noexcept {}
  };

  coroutine_handle<promise_type> h_;
  operator coroutine_handle<promise_type>() const { return h_; }
};

template<typename PromiseType>
struct GetPromise {
  PromiseType* p_;
  bool await_ready() { return false; } // says yes call await_suspend
  bool await_suspend(coroutine_handle<PromiseType> h) {
    p_ = &h.promise();
    return false;         // says no don't suspend coroutine after all
  }
  PromiseType* await_resume() { return p_; }
};

ReturnObject3
counter3()
{
  auto pp = co_await GetPromise<ReturnObject3::promise_type>{};

  for (unsigned i = 0;; ++i) {
    pp->value_ = i;
    co_await suspend_always{};
  }
}

void
main3()
{
  coroutine_handle<ReturnObject3::promise_type> h = counter3();
  ReturnObject3::promise_type& promise = h.promise();
  for (int i = 0; i < 3; ++i) {
    cout << "counter3: " << promise.value_ << endl;
    h();
  }
  h.destroy();
}

struct ReturnObject4 {
  struct promise_type {
    unsigned value_;

    ReturnObject4 get_return_object() {
      return {
        .h_ = coroutine_handle<promise_type>::from_promise(*this)
      };
    }
    suspend_never initial_suspend() { return {}; }
    suspend_never final_suspend() { return {}; }
    void unhandled_exception() {}
    void return_void() noexcept {}
    suspend_always yield_value(unsigned value) {
      value_ = value;
      return {};
    }
  };

  coroutine_handle<promise_type> h_;
};

ReturnObject4
counter4()
{
  for (unsigned i = 0;; ++i)
    co_yield i;       // co yield i => co_await promise.yield_value(i)
}

void
main4()
{
  auto h = counter4().h_;
  auto& promise = h.promise();
  for (int i = 0; i < 3; ++i) {
    cout << "counter4: " << promise.value_ << endl;
    h();
  }
  h.destroy();
}

struct ReturnObject5 {
  struct promise_type {
    unsigned value_;

    ~promise_type() { cout << "promise_type destroyed" << endl; }
    ReturnObject5 get_return_object() {
      return {
        .h_ = coroutine_handle<promise_type>::from_promise(*this)
      };
    }
    suspend_never initial_suspend() { return {}; }
    suspend_always final_suspend() { return {}; }
    void unhandled_exception() {}
    suspend_always yield_value(unsigned value) {
      value_ = value;
      return {};
    }
    void return_void() {}
  };

  coroutine_handle<promise_type> h_;
};

ReturnObject5
counter5()
{
  for (unsigned i = 0; i < 3; ++i)
    co_yield i;
  // falling off end of function or co_return; => promise.return_void();
  // (co_return value; => promise.return_value(value);)
}

void
main5()
{
  auto h = counter5().h_;
  auto& promise = h.promise();
  while (!h.done()) { // Do NOT use while(h) (which checks h non-NULL)
    cout << "counter5: " << promise.value_ << endl;
    h();
  }
  h.destroy();
}

template<typename T>
struct Generator {
  struct promise_type;
  using handle_type = coroutine_handle<promise_type>;

  struct promise_type {
    T value_;
    exception_ptr exception_;

    Generator get_return_object() {
      return Generator(handle_type::from_promise(*this));
    }
    suspend_always initial_suspend() { return {}; }
    suspend_always final_suspend() { return {}; }
    void unhandled_exception() { exception_ = current_exception(); }
    template<convertible_to<T> From> // C++20 concept
    suspend_always yield_value(From&& from) {
      value_ = forward<From>(from);
      return {};
    }
    void return_void() {}
  };

  handle_type h_;

  Generator(handle_type h) : h_(h) {}
  ~Generator() { h_.destroy(); }
  explicit operator bool() {
    fill();
    return !h_.done();
  }
  T operator()() {
    fill();
    full_ = false;
    return move(h_.promise().value_);
  }

private:
  bool full_ = false;

  void fill() {
    if (!full_) {
      h_();
      if (h_.promise().exception_)
        rethrow_exception(h_.promise().exception_);
      full_ = true;
    }
  }
};

Generator<unsigned>
counter6()
{
  for (unsigned i = 0; i < 3;)
    co_yield i++;
}

void
main6()
{
  auto gen = counter6();
  while (gen)
    cout << "counter6: " << gen() << endl;
}

int
main()
{
  main1();
  main2();
  main3();
  main4();
  main5();
  main6();
}
