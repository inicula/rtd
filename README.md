# rtd

Minimalistic regular expression to DFA conversion using Dijkstra's 'shunting
yard' algorithm, followed by Thompson's construction algorithm, followed by the
powerset construction algorithm.

## Operators

* `|` - union;
* `*` - Kleene star;
* concatenation (implicit).

## Building

### Dependencies:

* [`fmt`](https://fmt.dev/latest/index.html);
* [`graphviz`](https://graphviz.org/docs/library/);
* `pkg-config`.

On Debian-based systems, they can usually be installed as follows:

```bash
$ sudo apt install pkg-config libfmt-dev libgraphviz-dev
```

To clone and build the program, run:

```bash
$ git clone https://github.com/niculaionut/rtd.git
$ cd rtd
$ make
```

## Resources

* ['Shunting yard' algorithm](https://www.engr.mun.ca/~theo/Misc/exp_parsing.htm);
* [Thompson's construction algorithm](https://en.wikipedia.org/wiki/Thompson%27s_construction);
* [Powerset construction algorithm](https://en.wikipedia.org/wiki/Powerset_construction);
* [`graphviz` example](https://gitlab.com/graphviz/graphviz/-/blob/main/dot.demo/example.c).
