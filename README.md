# Tasker

Implement task system to processing data asynchronously

## Usage

### Construct with processing function

```cpp
#include "tasker.h"

int main()
{
    Tasker<int> tasker { [](auto value) { std::cout << value; } };

    tasker.Post(42);

    return 0;
}
```

[Compiler Explorer](https://godbolt.org/z/T4b4vP5h3)

### Use as a member variable

```cpp
#include "tasker.h"

class A {
public:
    A()
        : tasker_ { [this](auto value) { Process(std::move(value)); } }
    {
    }

    ~A()
    {
        // Need to stop tasker before releasing other member variable
        tasker_.Stop();
    }

    void Send(int value)
    {
        tasker_.Post(value);
    }

private:
    void Process(int value)
    {
        std::cout << value;
    }

    Tasker<int> tasker_;
};

int main()
{
    A a;

    a.Send(42);

    return 0;
}
```

[Compiler Explorer](https://godbolt.org/z/scoz3539v)

## Reference

- [Better Code: Concurrency - Sean Parent](https://www.youtube.com/watch?v=zULU6Hhp42w)
