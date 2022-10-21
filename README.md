# channel

[![test](https://github.com/lesomnus/channel-cpp/actions/workflows/test.yaml/badge.svg)](https://github.com/lesomnus/channel-cpp/actions/workflows/test.yaml)
[![codecov](https://codecov.io/github/lesomnus/channel-cpp/branch/main/graph/badge.svg?token=DbqDiCe09i)](https://codecov.io/github/lesomnus/channel-cpp)


C++ implementation of [Go channel](https://go.dev/ref/spec#Channel_types).

## Usage

### Send/Recv

```cpp
auto const chan = make_chan<int>(); // Channel with 0 size buffer.

auto const sender = std::jthread([&](){
	chan->send(42);
});

int v;
chan->recv(v);
```

```go
/* Golang equivalent. */

c := make(chan int)

go func(){ c <- 42 }()

answer := <- c
```


### Select

```cpp
auto const chan1 = make_chan<int>();
auto const chan2 = make_chan<int>();

auto const sender = std::jthread([&](){
	chan2->send(42);
});

select(
	recv(*chan1, [](bool ok, int v){
		std::cout << "chan1 received " << v << std::endl;
	}),
	recv(*chan2, [](bool ok, int v){
		std::cout << "chan2 received " << v << std::endl;
	}),
);

// This blocks forever because the receive from `chan1` is effectively cancelled.
chan1->send(36);
```

```go
/* Golang equivalent. */

c1 := make(chan int)
c2 := make(chan int)

go func(){ c2 <- 42 }()

select {
	case v := <- c1: fmt.Println("c1 received", v)
	case v := <- c2: fmt.Println("c2 received", v)
}
```
