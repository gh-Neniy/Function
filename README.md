# Function

Реализация `Function` (аналог std::function) и `MoveOnlyFunction` (аналог std::move_only_function) для хранения и вызова любых callable-объектов.

## Возможности

- Хранение указателей на функции, объектов лямбд с захватами и функциональных объектов, указателей на методы и на поля классов
- Поддержка **копирования и перемещения** в Function и **только перемещения** в MoveOnlyFunction (без требования копируемости)

## Пример использования

```cpp
#include "function.h"
#include <iostream>

struct A {
  int sum(int x, int y) {
    return x + y + z;
  }

  int z = 0;
};

int sum(int x, int y) {
  return x + y;
}

int main() {
  Function<int(int, int)> func = &sum; // указатель на функцию
  std::cout << func(5, 5) << '\n'; // 10

  int (A::*sum_method_ptr)(int, int) = &A::sum; // указатель на метод
  Function<int(A&, int, int)> func2 = sum_method_ptr;
  A object{7};
  std::cout << func2(object, 1, 3) << '\n'; // 11

  int z = 4;
  func = [z](int x, int y){ return x + y + z; }; // лямбда
  std::cout << func(5, 5) << '\n'; // 14

  int A::* field_ptr = &A::z; // указатель на поле
  Function<int(A&)> func3 = field_ptr;
  std::cout << func3(object) << '\n'; // 7
}
```
