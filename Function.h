#pragma once
#include <functional>
#include <typeinfo>

#ifndef NENIY_FUNCTION
#define NENIY_FUNCTION

template <bool IsMoveOnly, typename T>
class FunctionBase; // Для типов, которые не являются функциональными

template <bool IsMoveOnly, typename ReturnType, typename... Args>
class FunctionBase<IsMoveOnly, ReturnType(Args...)> {
 public:
  FunctionBase() noexcept {}
  FunctionBase(std::nullptr_t) noexcept {}

  template <typename F>
  requires (
    std::conditional_t<IsMoveOnly, std::true_type, std::is_copy_constructible<std::decay_t<F>>>::value &&
    std::is_invocable_r_v<ReturnType, std::decay_t<F>, Args...> &&
    !std::is_same_v<FunctionBase, std::remove_cvref_t<F>>
  )
  FunctionBase(F&& function) : invoke_pointer(reinterpret_cast<InvokePointerType>(Invoke<std::decay_t<F>>)),
                               manage_block_pointer(CreateManageBlock(std::forward<F>(function))) {
    if constexpr (sizeof(std::decay_t<F>) > BUFFER_SIZE) {
      function_pointer = new std::decay_t<F>(std::forward<F>(function));
    } else {
      new (buffer) std::decay_t<F>(std::forward<F>(function));
      function_pointer = buffer;
    }
  }

  FunctionBase(const FunctionBase& other) 
  requires (!IsMoveOnly) : invoke_pointer(other.invoke_pointer),
                           manage_block_pointer(other.manage_block_pointer) {
    if (manage_block_pointer != nullptr) {
      manage_block_pointer->copy_from_to(other, *this);
    }
  }

  FunctionBase(FunctionBase&& other) noexcept : invoke_pointer(std::move(other.invoke_pointer)),
                                                manage_block_pointer(std::move(other.manage_block_pointer)) {
    if (manage_block_pointer != nullptr) {
      manage_block_pointer->move_from_to(std::move(other), *this);
    }
    other.invoke_pointer = nullptr;
    other.manage_block_pointer = nullptr;
  }

  FunctionBase& operator=(const FunctionBase& other)
  requires (!IsMoveOnly) {
    manage_block_pointer = other.manage_block_pointer;
    if (manage_block_pointer != nullptr) {
      manage_block_pointer->copy_assignment_from_to(other, *this);
    }
    invoke_pointer = other.invoke_pointer;
    return *this;
  }

  FunctionBase& operator=(FunctionBase&& other) noexcept {
    manage_block_pointer = other.manage_block_pointer;
    if (manage_block_pointer != nullptr) {
      manage_block_pointer->move_assignment_from_to(std::move(other), *this);
    } 
    invoke_pointer = other.invoke_pointer;
    other.manage_block_pointer = nullptr;
    other.function_pointer = nullptr;
    other.invoke_pointer = nullptr;
    return *this;
  }

  template <typename F>
  requires (
    std::conditional_t<IsMoveOnly, std::true_type, std::is_copy_constructible<std::decay_t<F>>>::value &&
    std::is_invocable_r_v<ReturnType, std::decay_t<F>, Args...> &&
    !std::is_same_v<FunctionBase, std::remove_cvref_t<F>>
  )
  FunctionBase& operator=(F&& function) {
    if (manage_block_pointer) {
      manage_block_pointer->destroy(function_pointer);
    }
    manage_block_pointer = CreateManageBlock(std::forward<F>(function));
    invoke_pointer = reinterpret_cast<InvokePointerType>(Invoke<std::decay_t<F>>);

    if constexpr (sizeof(std::decay_t<F>) > BUFFER_SIZE) {
      function_pointer = new std::decay_t<F>(std::forward<F>(function));
    } else {
      new (buffer) std::decay_t<F>(std::forward<F>(function));
      function_pointer = buffer;
    }
    return *this;
  }

  template <typename F>
  FunctionBase& operator=(std::reference_wrapper<F> wrapper) noexcept {
    return operator=(wrapper.get());
  }

  ~FunctionBase() {
    if (manage_block_pointer != nullptr) {
      manage_block_pointer->destroy(function_pointer);
    }
  }

  ReturnType operator()(Args... args) const {
    if (function_pointer == nullptr) {
      throw std::bad_function_call();
    }
    return invoke_pointer(function_pointer, std::forward<Args>(args)...);
  }

  template <typename F>
  F* target() noexcept {
    return reinterpret_cast<F*>(function_pointer);
  }

  template <typename F>
  const F* target() const noexcept {
    return reinterpret_cast<F*>(function_pointer); 
  }

  const std::type_info& target_type() const noexcept {
    return manage_block_pointer->get_type();
  }

  explicit operator bool() const noexcept {
    return function_pointer != nullptr;
  }

  bool operator==(nullptr_t) const noexcept {
    return function_pointer == nullptr;
  }

  bool operator!=(nullptr_t) const noexcept {
    return !operator==(nullptr);
  }

  friend bool operator==(nullptr_t, const FunctionBase& function) {
    return function.function_pointer == nullptr;
  }

  friend bool operator!=(nullptr_t, const FunctionBase& function) {
    return function.function_pointer != nullptr;
  }

 private:
  static constexpr std::size_t BUFFER_SIZE = 16;
  alignas(std::max_align_t) std::byte buffer[BUFFER_SIZE];
  void* function_pointer = nullptr;

  using InvokePointerType = ReturnType(*)(void*, Args...);
  using CopyPointerType = void(*)(const FunctionBase&, FunctionBase&);
  using MovePointerType = void(*)(FunctionBase&&, FunctionBase&);
  using CopyAssignmentPointerType = void(*)(const FunctionBase&, FunctionBase&);
  using MoveAssignmentPointerType = void(*)(FunctionBase&&, FunctionBase&);
  using DestroyPointerType = void(*)(void*);
  using GetTypePointerType = const std::type_info&(*)();

  struct ManageBlock {
    template <typename F>
    requires (IsMoveOnly)
    ManageBlock(F&&) : copy_from_to(nullptr),
                                copy_assignment_from_to(nullptr),
                                move_from_to(reinterpret_cast<MovePointerType>(Move<std::decay_t<F>>)),
                                move_assignment_from_to(reinterpret_cast<MoveAssignmentPointerType>(MoveAssignment<std::decay_t<F>>)),
                                destroy(reinterpret_cast<DestroyPointerType>(Destroy<std::decay_t<F>>)),
                                get_type(reinterpret_cast<GetTypePointerType>(GetType<std::decay_t<F>>)) {}

    template <typename F>
    requires (!IsMoveOnly)
    ManageBlock(F&&) : copy_from_to(reinterpret_cast<CopyPointerType>(Copy<std::decay_t<F>>)),
                                copy_assignment_from_to(reinterpret_cast<CopyAssignmentPointerType>(CopyAssignment<std::decay_t<F>>)),
                                move_from_to(reinterpret_cast<MovePointerType>(Move<std::decay_t<F>>)),
                                move_assignment_from_to(reinterpret_cast<MoveAssignmentPointerType>(MoveAssignment<std::decay_t<F>>)),
                                destroy(reinterpret_cast<DestroyPointerType>(Destroy<std::decay_t<F>>)),
                                get_type(reinterpret_cast<GetTypePointerType>(GetType<std::decay_t<F>>)) {}

    CopyPointerType copy_from_to;
    CopyAssignmentPointerType copy_assignment_from_to;
    MovePointerType move_from_to;
    MoveAssignmentPointerType move_assignment_from_to;
    DestroyPointerType destroy;
    GetTypePointerType get_type;
  };

  template <typename F>
  static ManageBlock* CreateManageBlock(F&& function) {
    static ManageBlock manage_block(std::forward<F>(function));
    return &manage_block;
  }

  InvokePointerType invoke_pointer = nullptr;
  ManageBlock* manage_block_pointer = nullptr;
  
  // Методы для обработки фукнционального объекта
  template <typename F>
  static ReturnType Invoke(F* function_ptr, Args... args) {
    return std::invoke(*function_ptr, std::forward<Args>(args)...);
  }

  template <typename F, bool IsCopy, typename Source>
  static void CopyOrMove(Source&& source, FunctionBase& destination) {
    if constexpr (sizeof(std::decay_t<F>) > BUFFER_SIZE) {
      if constexpr (IsCopy) {
        destination.function_pointer = new F(*reinterpret_cast<F*>(source.function_pointer));
      } else {
        destination.function_pointer = std::move(source.function_pointer);
        source.function_pointer = nullptr;
      }
    } else {
      if constexpr (IsCopy) { // тут через std::forward убрать копипасту никак, потому что F не является универсальной ссылкой
        new (destination.buffer) F(*reinterpret_cast<F*>(source.function_pointer));
      } else {
        new (destination.buffer) F(std::move(*reinterpret_cast<F*>(source.function_pointer)));
        reinterpret_cast<F*>(std::move(source.buffer))->~F();
        source.function_pointer = nullptr;
      }
      destination.function_pointer = destination.buffer;
    }
  }

  template <typename F>
  static void Copy(const FunctionBase& source, FunctionBase& destination) {
    return CopyOrMove<F, true>(source, destination);
  }

  template <typename F>
  static void Move(FunctionBase&& source, FunctionBase& destination) {
    return CopyOrMove<F, false>(std::move(source), destination);
  }

  template <typename F, bool IsCopy, typename Source>
  static void Assignment(Source&& source, FunctionBase& destination) {
    if constexpr (sizeof(std::decay_t<F>) > BUFFER_SIZE) {
      delete reinterpret_cast<F*>(destination.function_pointer);
      destination.function_pointer = new F(std::forward<Source>(source).function_pointer);
    } else {
      reinterpret_cast<F*>(destination.buffer)->~F();
      if constexpr (IsCopy) {
        new (destination.buffer) F(*reinterpret_cast<F*>(const_cast<FunctionBase&>(source).buffer));
      } else {
        new (destination.buffer) F(std::move(*reinterpret_cast<F*>(source.buffer)));
      }
      destination.function_pointer = destination.buffer;
    }
  }

  template <typename F>
  static void CopyAssignment(const FunctionBase& source, FunctionBase& destination) {
    return Assignment<F, true>(source, destination);
  }

  template <typename F>
  static void MoveAssignment(FunctionBase&& source, FunctionBase& destination) {
    return Assignment<F, false>(std::move(source), destination);
  }

  template <typename F>
  static void Destroy(F* function_ptr) {
    if constexpr (sizeof(std::decay_t<F>) > BUFFER_SIZE) {
      delete function_ptr;
    } else {
      function_ptr->~F();
    }
  }

  template <typename F>
  static const std::type_info& GetType() {
    return typeid(F);
  }
};

template <typename ReturnType, typename... Args>
FunctionBase(ReturnType(*)(Args...)) -> FunctionBase<false, ReturnType(Args...)>;
template <typename ReturnType, typename... Args>
FunctionBase(ReturnType(*)(Args...)) -> FunctionBase<true, ReturnType(Args...)>;

template <typename T>
struct LambdaTraits;

template <typename Callable, typename ReturnType, typename... Args>
struct LambdaTraits<ReturnType (Callable::*)(Args...) const> {
  using signature = ReturnType(Args...);
};

template <typename LambdaType>
FunctionBase(LambdaType) -> FunctionBase<false,
  typename LambdaTraits<decltype(&std::remove_reference_t<LambdaType>::operator())>::signature
>;
template <typename LambdaType>
FunctionBase(LambdaType) -> FunctionBase<true,
  typename LambdaTraits<decltype(&std::remove_reference_t<LambdaType>::operator())>::signature
>;

template <typename T>
using Function = FunctionBase<false, T>;
template <typename T>
using MoveOnlyFunction = FunctionBase<true, T>;

#endif // NENIY_FUNCTION
