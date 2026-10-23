// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// helix::Callback<R(Args...)>: a move-only std::function for hot choke-point
// APIs, built to cost less flash per lambda type.
//
// std::function emits a manager per stored type (clone, destroy, address,
// type query), and on a 32-bit target it stores only 8 bytes inline, so
// almost every capture is heap-stored behind a manager of 80 B or more. Here:
//   - a trivially copyable callable that fits the inline buffer has NO
//     per-type manager: moving it copies the buffer, destroying it is nothing;
//   - any other callable that fits gets a per-type move+destroy manager;
//   - an oversized callable lives on the heap behind a destroy-only manager,
//     since moving it moves only the pointer.
// The invoker is per type in every case; it is the callable's own body.
//
// Move-only, so a callable may capture a unique_ptr. No RTTI, no exceptions:
// calling an empty Callback goes through throw_or_abort().
//
// helix::SharedCallback<R(Args...)> is the copyable sibling, for APIs whose
// callbacks are copied into wrappers, tables and retry paths. A small trivially
// copyable callable is stored inline exactly as in Callback; any other callable
// lives in one heap block with an atomic reference count, so copying bumps the
// count and the per-type code is a destroy function only. Copies share that one
// callable: a `mutable` callable's state is shared between them rather than
// cloned, and its captures are destroyed with the last copy, on whichever
// thread releases it.

#include "exception_policy.h"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace helix {

namespace callback_detail {

/// Inline capacity in pointer-sized words. Eight holds `this` plus a
/// shared_ptr token, a generation and a tag (AsyncLifetimeGuard::defer's
/// wrapper) with a few words to spare on a 32-bit target.
inline constexpr std::size_t kInlineWords = 8;
inline constexpr std::size_t kInlineSize = kInlineWords * sizeof(void*);
inline constexpr std::size_t kInlineAlign = alignof(std::max_align_t);

enum class Op { Relocate, Destroy };

/// Out of line so every signature shares one copy of the abort path.
[[noreturn]] inline __attribute__((noinline)) void call_empty() {
    throw_or_abort(std::bad_function_call());
}

template <typename D>
inline constexpr bool kStoredInline = sizeof(D) <= kInlineSize && alignof(D) <= kInlineAlign &&
                                      std::is_nothrow_move_constructible_v<D>;

template <typename D>
inline constexpr bool kTrivialInline = kStoredInline<D> && std::is_trivially_copyable_v<D>;

} // namespace callback_detail

template <typename Sig> class Callback;
template <typename Sig> class SharedCallback;

namespace callback_detail {

/// Callables that have an empty state, which a Callback adopts as its own.
template <typename T>
struct IsNullable : std::bool_constant<std::is_pointer_v<T> || std::is_member_pointer_v<T>> {};
template <typename S> struct IsNullable<std::function<S>> : std::true_type {};
template <typename S> struct IsNullable<Callback<S>> : std::true_type {};
template <typename S> struct IsNullable<SharedCallback<S>> : std::true_type {};

/// Reference-count header of a SharedCallback heap block.
struct SharedBlock {
    std::atomic<unsigned> refs{1};
};

template <typename D> struct SharedBox : SharedBlock {
    template <typename F> explicit SharedBox(F&& f) : value(std::forward<F>(f)) {}
    D value;
};

} // namespace callback_detail

/// Where a Callback keeps a callable of type F, and what that costs in code.
enum class CallbackStorage {
    Trivial, ///< inline, no per-type manager
    Inline,  ///< inline, per-type move+destroy manager
    Heap,    ///< heap-allocated, per-type destroy manager
};

template <typename F> constexpr CallbackStorage callback_storage() {
    using D = std::decay_t<F>;
    if constexpr (callback_detail::kTrivialInline<D>) {
        return CallbackStorage::Trivial;
    } else if constexpr (callback_detail::kStoredInline<D>) {
        return CallbackStorage::Inline;
    } else {
        return CallbackStorage::Heap;
    }
}

template <typename R, typename... Args> class Callback<R(Args...)> {
    template <typename F>
    using EnableIfCallable = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Callback> &&
                                              std::is_invocable_r_v<R, std::decay_t<F>&, Args...>>;

  public:
    Callback() noexcept = default;
    Callback(std::nullptr_t) noexcept {}

    template <typename F, typename = EnableIfCallable<F>> Callback(F&& f) {
        using D = std::decay_t<F>;
        if constexpr (callback_detail::IsNullable<D>::value) {
            if (!f) {
                return;
            }
        }
        if constexpr (callback_detail::kStoredInline<D>) {
            ::new (static_cast<void*>(storage_)) D(std::forward<F>(f));
            invoke_ = &invoke_inline<D>;
            if constexpr (!callback_detail::kTrivialInline<D>) {
                manage_ = &manage_inline<D>;
            }
        } else {
            D* heap = new D(std::forward<F>(f));
            std::memcpy(storage_, &heap, sizeof(heap));
            invoke_ = &invoke_heap<D>;
            manage_ = &manage_heap<D>;
        }
    }

    Callback(Callback&& other) noexcept {
        take(other);
    }

    Callback& operator=(Callback&& other) noexcept {
        if (this != &other) {
            reset();
            take(other);
        }
        return *this;
    }

    Callback& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    template <typename F, typename = EnableIfCallable<F>> Callback& operator=(F&& f) {
        return *this = Callback(std::forward<F>(f));
    }

    Callback(const Callback&) = delete;
    Callback& operator=(const Callback&) = delete;

    ~Callback() {
        reset();
    }

    explicit operator bool() const noexcept {
        return invoke_ != nullptr;
    }

    friend bool operator==(const Callback& cb, std::nullptr_t) noexcept {
        return !cb;
    }
    friend bool operator!=(const Callback& cb, std::nullptr_t) noexcept {
        return static_cast<bool>(cb);
    }

    /// Like std::function, a const call may run a `mutable` callable.
    R operator()(Args... args) const {
        if (invoke_ == nullptr) {
            callback_detail::call_empty();
        }
        return invoke_(storage_, std::forward<Args>(args)...);
    }

  private:
    using Invoker = R (*)(void* storage, Args&&... args);
    using Manager = void (*)(callback_detail::Op op, void* dst, void* src);

    template <typename D> static R invoke_inline(void* storage, Args&&... args) {
        return std::invoke(*std::launder(static_cast<D*>(storage)), std::forward<Args>(args)...);
    }

    template <typename D> static R invoke_heap(void* storage, Args&&... args) {
        D* heap;
        std::memcpy(&heap, storage, sizeof(heap));
        return std::invoke(*heap, std::forward<Args>(args)...);
    }

    template <typename D> static void manage_inline(callback_detail::Op op, void* dst, void* src) {
        D* from = std::launder(static_cast<D*>(src));
        if (op == callback_detail::Op::Relocate) {
            ::new (dst) D(std::move(*from));
        }
        from->~D();
    }

    template <typename D> static void manage_heap(callback_detail::Op op, void* dst, void* src) {
        if (op == callback_detail::Op::Relocate) {
            std::memcpy(dst, src, sizeof(D*));
            return;
        }
        D* heap;
        std::memcpy(&heap, src, sizeof(heap));
        delete heap;
    }

    void take(Callback& other) noexcept {
        if (other.invoke_ == nullptr) {
            return;
        }
        if (other.manage_ == nullptr) {
            std::memcpy(storage_, other.storage_, sizeof(storage_));
        } else {
            other.manage_(callback_detail::Op::Relocate, storage_, other.storage_);
        }
        invoke_ = other.invoke_;
        manage_ = other.manage_;
        other.invoke_ = nullptr;
        other.manage_ = nullptr;
    }

    void reset() noexcept {
        if (manage_ != nullptr) {
            manage_(callback_detail::Op::Destroy, nullptr, storage_);
        }
        invoke_ = nullptr;
        manage_ = nullptr;
    }

    alignas(
        callback_detail::kInlineAlign) mutable unsigned char storage_[callback_detail::kInlineSize];
    Invoker invoke_ = nullptr;
    Manager manage_ = nullptr;
};

namespace callback_detail {

/// Signature-independent half of SharedCallback. Copy, move and release are
/// the same bytes for every signature, so they live here out of line: each
/// call site that copies or drops a SharedCallback emits one call.
class SharedCallbackBase {
  public:
    explicit operator bool() const noexcept {
        return invoke_ != nullptr;
    }

  protected:
    using ErasedInvoker = void (*)();
    using Destroyer = void (*)(SharedBlock* block);

    SharedCallbackBase() noexcept = default;
    SharedCallbackBase(const SharedCallbackBase& other) noexcept {
        copy_from(other);
    }
    SharedCallbackBase(SharedCallbackBase&& other) noexcept {
        steal(other);
    }
    ~SharedCallbackBase() {
        reset();
    }

    SharedCallbackBase& operator=(const SharedCallbackBase&) = delete;
    SharedCallbackBase& operator=(SharedCallbackBase&&) = delete;

    __attribute__((noinline)) void copy_from(const SharedCallbackBase& other) noexcept {
        std::memcpy(storage_, other.storage_, sizeof(storage_));
        invoke_ = other.invoke_;
        destroy_ = other.destroy_;
        if (destroy_ != nullptr) {
            block()->refs.fetch_add(1, std::memory_order_relaxed);
        }
    }

    __attribute__((noinline)) void steal(SharedCallbackBase& other) noexcept {
        std::memcpy(storage_, other.storage_, sizeof(storage_));
        invoke_ = other.invoke_;
        destroy_ = other.destroy_;
        other.invoke_ = nullptr;
        other.destroy_ = nullptr;
    }

    __attribute__((noinline)) void reset() noexcept {
        if (destroy_ != nullptr) {
            SharedBlock* b = block();
            if (b->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                destroy_(b);
            }
        }
        invoke_ = nullptr;
        destroy_ = nullptr;
    }

    void assign_copy(const SharedCallbackBase& other) noexcept {
        if (this != &other) {
            reset();
            copy_from(other);
        }
    }

    void assign_move(SharedCallbackBase& other) noexcept {
        if (this != &other) {
            reset();
            steal(other);
        }
    }

    SharedBlock* block() const noexcept {
        SharedBlock* b;
        std::memcpy(&b, storage_, sizeof(b));
        return b;
    }

    alignas(kInlineAlign) mutable unsigned char storage_[kInlineSize];
    ErasedInvoker invoke_ = nullptr;
    Destroyer destroy_ = nullptr;
};

} // namespace callback_detail

template <typename R, typename... Args>
class SharedCallback<R(Args...)> : public callback_detail::SharedCallbackBase {
    template <typename F>
    using EnableIfCallable = std::enable_if_t<!std::is_same_v<std::decay_t<F>, SharedCallback> &&
                                              std::is_invocable_r_v<R, std::decay_t<F>&, Args...>>;
    using Invoker = R (*)(void* storage, Args&&... args);

  public:
    SharedCallback() noexcept = default;
    SharedCallback(std::nullptr_t) noexcept {}
    SharedCallback(const SharedCallback& other) noexcept = default;
    SharedCallback(SharedCallback&& other) noexcept = default;

    template <typename F, typename = EnableIfCallable<F>> SharedCallback(F&& f) {
        using D = std::decay_t<F>;
        if constexpr (callback_detail::IsNullable<D>::value) {
            if (!f) {
                return;
            }
        }
        if constexpr (callback_detail::kTrivialInline<D>) {
            ::new (static_cast<void*>(storage_)) D(std::forward<F>(f));
            set_invoker(&invoke_inline<D>);
        } else {
            callback_detail::SharedBlock* b = new callback_detail::SharedBox<D>(std::forward<F>(f));
            std::memcpy(storage_, &b, sizeof(b));
            set_invoker(&invoke_shared<D>);
            destroy_ = &destroy_shared<D>;
        }
    }

    SharedCallback& operator=(const SharedCallback& other) noexcept {
        assign_copy(other);
        return *this;
    }

    SharedCallback& operator=(SharedCallback&& other) noexcept {
        assign_move(other);
        return *this;
    }

    SharedCallback& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    template <typename F, typename = EnableIfCallable<F>> SharedCallback& operator=(F&& f) {
        SharedCallback tmp(std::forward<F>(f));
        assign_move(tmp);
        return *this;
    }

    friend bool operator==(const SharedCallback& cb, std::nullptr_t) noexcept {
        return !cb;
    }
    friend bool operator!=(const SharedCallback& cb, std::nullptr_t) noexcept {
        return static_cast<bool>(cb);
    }

    /// Like std::function, a const call may run a `mutable` callable.
    R operator()(Args... args) const {
        if (invoke_ == nullptr) {
            callback_detail::call_empty();
        }
        return reinterpret_cast<Invoker>(invoke_)(storage_, std::forward<Args>(args)...);
    }

  private:
    void set_invoker(Invoker fn) noexcept {
        invoke_ = reinterpret_cast<ErasedInvoker>(fn);
    }

    template <typename D> static R invoke_inline(void* storage, Args&&... args) {
        return std::invoke(*std::launder(static_cast<D*>(storage)), std::forward<Args>(args)...);
    }

    template <typename D> static R invoke_shared(void* storage, Args&&... args) {
        callback_detail::SharedBlock* b;
        std::memcpy(&b, storage, sizeof(b));
        return std::invoke(static_cast<callback_detail::SharedBox<D>*>(b)->value,
                           std::forward<Args>(args)...);
    }

    template <typename D> static void destroy_shared(callback_detail::SharedBlock* block) {
        delete static_cast<callback_detail::SharedBox<D>*>(block);
    }
};

} // namespace helix
