#include <getopt.h>
#include <graphviz/gvc.h>
#include <array>
#include <vector>
#include <string>
#include <stack>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <numeric>
#include <optional>
#include <charconv>
#include <cassert>
#include "numtypes.hpp"

/* Macros */
/* clang-format off */
#define DEFAULT_ALPHABET    "abcdefghijklmnopqrstuvwxyz"
#define ALL_ALPHANUMS       "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890"
#define START_UNINITIALIZED (usize(-1))
#define START_COLOR         "turquoise"
#define FINAL_COLOR         "x11green"
#define START_FINAL_COLOR   START_COLOR ":" FINAL_COLOR
#define FONT                "monospace"
#define S_LAMBDA            '\0'
#define OP_CONCAT           '.'
#define OP_UNION            '|'
#define OP_KLEENE           '*'
#define OP_PLUS             '+'
#define OP_OPT              '?'
#define NUM_CHARS           (1 << 8)
#define LAMBDA_UTF          {char(0xce), char(0xbb)}

/* Enums */
enum class TokenType : u8 {
    REGULAR = 0,
    OPERATOR,
    LEFT_PAREN,
    RIGHT_PAREN,
    ERROR,
};

enum GraphNodeFlag : u32 {
    VISITED = 1 << 0,
    START   = 1 << 1,
    FINAL   = 1 << 2,
};
/* clang-format on */

/* Structs */
struct NFANode {
    NFANode* neighbours[2] = {};
    char symbols[2] = {};
    usize id = 0;
    bool visited = {};
};

struct NFAFragment {
    NFANode* start;
    NFANode* finish;
};

struct Edge {
    usize src;
    usize dest;
    char symbol;
};

struct Transition {
    usize dest;
    char symbol;
};

struct Graph {
    std::vector<std::vector<Transition>> adj;
    std::vector<u32> flags;
    usize start;
};

struct AgobjAttrs {
    const char* label = nullptr;
    const char* style = nullptr;
    const char* font = nullptr;
    const char* color = nullptr;
    const char* rankdir = nullptr;
};

/* Globals */
static bool in_alphabet[NUM_CHARS] = {};
static std::vector<NFANode*> node_ptrs;
static constexpr auto OP_PREC = []() {
    std::array<u8, NUM_CHARS> arr = {};
    arr[OP_KLEENE] = 3;
    arr[OP_PLUS] = 3;
    arr[OP_OPT] = 3;
    arr[OP_CONCAT] = 2;
    arr[OP_UNION] = 1;

    return arr;
}();

/* Functions declarations */
static bool operator<(const Transition&, const Transition&);
static bool operator==(const Transition&, const Transition&);
static TokenType type_of(char);
static std::string add_concatenation_op(const std::string_view);
static std::optional<std::string> get_postfix(const std::string_view);
static std::optional<NFANode*> get_nfa(const std::string_view);
static Graph to_graph(NFANode*);
static void add_transitive_closure_helper(usize, usize, std::vector<Transition>&, Graph&);
static void add_transitive_closure(Graph&);
static void remove_lambdas(Graph&);
static Graph to_dfa_graph(const Graph&);
static void print_components(const Graph&, FILE*);
static void set_attrs(void*, const AgobjAttrs&);
static void export_graph(const Graph&, FILE*, const std::string&);
static void usage();

/* Functions definitions  */
template<>
struct std::hash<std::unordered_set<usize>> {
    std::size_t
    operator()(const std::unordered_set<usize>& xs) const noexcept
    {
        std::size_t seed = 0;
        for (std::size_t x : xs)
            seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2); /* from boost::hash_combine */

        return seed;
    }
};

bool
operator<(const Transition& x, const Transition& y)
{
    return std::tie(x.dest, x.symbol) < std::tie(y.dest, y.symbol);
}

bool
operator==(const Transition& x, const Transition& y)
{
    return std::tie(x.dest, x.symbol) == std::tie(y.dest, y.symbol);
}

TokenType
type_of(char token)
{
    auto token_idx = u8(token);

    if (in_alphabet[token_idx])
        return TokenType::REGULAR;
    if (OP_PREC[token_idx])
        return TokenType::OPERATOR;
    if (token == '(')
        return TokenType::LEFT_PAREN;
    if (token == ')')
        return TokenType::RIGHT_PAREN;
    return TokenType::ERROR;
}

std::string
add_concatenation_op(const std::string_view infix)
{
    if (infix.empty())
        return "";

    std::string result{infix.substr(0, 1)};
    for (usize i = 1; i < infix.size(); ++i) {
        const char a = infix[i - 1];
        const char b = infix[i];
        const auto t_a = type_of(a);
        const auto t_b = type_of(b);

        /* Cases where the concatenation operator needs to be added */
        /* clang-format off */
        if ((t_a == TokenType::REGULAR && t_b == TokenType::REGULAR) ||
            (t_a == TokenType::REGULAR && b == '(') ||
            (a == OP_KLEENE && t_b == TokenType::REGULAR) ||
            (a == OP_KLEENE && b == '(') ||
            (a == OP_PLUS && t_b == TokenType::REGULAR) ||
            (a == OP_PLUS && b == '(') ||
            (a == OP_OPT && t_b == TokenType::REGULAR) ||
            (a == OP_OPT && b == '(') ||
            (a == ')' && t_b == TokenType::REGULAR) ||
            (a == ')' && b == '('))
            result += OP_CONCAT;
        /* clang-format on */

        result += b;
    }

    return result;
}

std::optional<std::string>
get_postfix(const std::string_view infix)
{
    /* Apply Dijkstra's 'shunting yard' algorithm */

    std::string postfix = "";
    std::stack<char, std::vector<char>> operators;
    for (char token : infix) {
        switch (type_of(token)) {
        case TokenType::REGULAR:
            postfix += token;
            break;
        case TokenType::OPERATOR:
            for (;;) {
                if (operators.empty())
                    break;

                auto top = operators.top();
                if (top == '(')
                    break;
                if (OP_PREC[u8(top)] < OP_PREC[u8(token)])
                    break;

                postfix += top;
                operators.pop();
            }

            operators.push(token);
            break;
        case TokenType::LEFT_PAREN:
            operators.push(token);
            break;
        case TokenType::RIGHT_PAREN:
            while (!operators.empty() && operators.top() != '(') {
                postfix += operators.top();
                operators.pop();
            }

            if (operators.empty() || operators.top() != '(')
                return std::nullopt;

            operators.pop();
            break;
        case TokenType::ERROR:
            return std::nullopt;
        }
    }

    while (!operators.empty()) {
        auto op = operators.top();
        if (op == '(')
            return std::nullopt;

        postfix += op;
        operators.pop();
    }

    return postfix;
}

std::optional<NFANode*>
get_nfa(const std::string_view postfix)
{
    /* Apply Thompson's construction algorithm */

    std::stack<NFAFragment, std::vector<NFAFragment>> nfa_components;
    for (char token : postfix) {
        NFANode *q, *f;

        if (token == OP_CONCAT || token == OP_UNION) {
            if (nfa_components.size() < 2)
                return std::nullopt;

            auto y = nfa_components.top();
            nfa_components.pop();
            auto x = nfa_components.top();
            nfa_components.pop();

            if (token == OP_CONCAT) {
                *(x.finish) = {{y.start}, {S_LAMBDA}};
                q = x.start;
                f = y.finish;
            } else {
                q = new NFANode{{x.start, y.start}, {S_LAMBDA, S_LAMBDA}};
                f = new NFANode{};
                *(x.finish) = {{f}, {S_LAMBDA}};
                *(y.finish) = {{f}, {S_LAMBDA}};
            }
        } else if (token == OP_KLEENE || token == OP_PLUS || token == OP_OPT) {
            if (nfa_components.empty())
                return std::nullopt;

            auto x = nfa_components.top();
            nfa_components.pop();

            f = new NFANode;
            q = new NFANode;

            if (token == OP_KLEENE) {
                *f = NFANode{};
                *q = NFANode{{x.start, f}, {S_LAMBDA, S_LAMBDA}};
                *(x.finish) = {{x.start, f}, {S_LAMBDA, S_LAMBDA}};
            } else if (token == OP_PLUS) {
                *f = NFANode{};
                *q = NFANode{{x.start}, {S_LAMBDA}};
                *(x.finish) = {{x.start, f}, {S_LAMBDA, S_LAMBDA}};
            } else {
                *f = NFANode{};
                *q = NFANode{{x.start, f}, {S_LAMBDA, S_LAMBDA}};
                *(x.finish) = {{f}, {S_LAMBDA}};
            }
        } else {
            f = new NFANode{};
            q = new NFANode{{f}, {token}};
        }

        nfa_components.push({q, f});
    }

    if (nfa_components.empty())
        return std::nullopt;

    return nfa_components.top().start;
}

void
to_graph_helper(NFANode* src, Graph& g)
{
    if (!src || src->visited)
        return;

    auto& [adj, flags, _] = g;

    node_ptrs.push_back(src);

    src->visited = true;
    src->id = adj.size(); /* Pre-order traversal, which is why `START_NODE_ID` is 0 */
    while (src->id >= adj.size()) {
        adj.push_back({});
        flags.push_back({});
    }

    if (g.start == START_UNINITIALIZED) {
        flags[src->id] |= START;
        g.start = src->id;
    }

    auto v0 = src->neighbours[0];
    auto v1 = src->neighbours[1];

    flags[src->id] |= FINAL * (!v0 && !v1);

    if (v0) {
        to_graph_helper(v0, g);
        adj[src->id].push_back({v0->id, src->symbols[0]});
    }
    if (v1) {
        to_graph_helper(v1, g);
        adj[src->id].push_back({v1->id, src->symbols[1]});
    }
}

Graph
to_graph(NFANode* src)
{
    Graph g = {};
    g.start = START_UNINITIALIZED;
    to_graph_helper(src, g);

    assert(g.start != START_UNINITIALIZED);
    return g;
}

void
add_transitive_closure_helper(usize from, usize src, std::vector<Transition>& to_add, Graph& g)
{
    auto& [adj, flags, _] = g;

    if (flags[src] & VISITED)
        return;

    flags[src] |= VISITED;

    for (auto [dest, symbol] : adj[src]) {
        if (symbol == S_LAMBDA) {
            to_add.push_back({dest, symbol});
            flags[from] |= flags[dest] & FINAL;

            add_transitive_closure_helper(from, dest, to_add, g);
        }
    }
}

void
add_transitive_closure(Graph& g)
{
    auto& [adj, flags, _] = g;

    std::vector<Transition> to_add;
    for (usize src = 0; src < adj.size(); ++src) {
        for (auto& f : flags)
            f &= ~VISITED;
        add_transitive_closure_helper(src, src, to_add, g);

        adj[src].insert(adj[src].end(), to_add.begin(), to_add.end());
        to_add.clear();
    }
}

void
remove_lambdas(Graph& g)
{
    auto& adj = g.adj;

    for (usize u = 0; u < adj.size(); ++u) {
        std::vector<Transition> to_add;
        for (auto [v, to_v] : adj[u]) {
            if (to_v != S_LAMBDA)
                continue;

            for (auto [w, to_w] : adj[v]) {
                if (to_w != S_LAMBDA)
                    to_add.push_back({w, to_w});
            }
        }

        adj[u].insert(adj[u].end(), to_add.begin(), to_add.end());
    }

    for (auto& ts : adj) {
        auto begin_of_lambda =
            std::partition(ts.begin(), ts.end(), [](auto& t) { return t.symbol != S_LAMBDA; });
        ts.erase(begin_of_lambda, ts.end());

        std::sort(ts.begin(), ts.end());
        auto begin_of_duplicates = std::unique(ts.begin(), ts.end());
        ts.erase(begin_of_duplicates, ts.end());
    }
}

Graph
to_dfa_graph(const Graph& nfa)
{
    static constexpr usize first_id = 1;
    usize next_id = first_id;
    std::vector<Edge> edges;
    std::queue<std::unordered_set<usize>> queue;
    std::unordered_map<std::unordered_set<usize>, usize> ids;
    std::unordered_set<usize> finals;

    queue.push({nfa.start});
    ids.insert({{nfa.start}, next_id++});
    while (!queue.empty()) {
        auto src_subset = std::move(queue.front());
        queue.pop();

        auto src_subset_id = ids[src_subset];

        /* Check if this subset will become a final node */
        for (auto src : src_subset) {
            if (nfa.flags[src] & FINAL) {
                finals.insert(src_subset_id);
                break;
            }
        }

        /* Create edges from the source subset through each symbol */
        for (char target_symbol = 'a'; target_symbol <= 'z'; ++target_symbol) {
            std::unordered_set<usize> dest_subset;
            for (auto src : src_subset) {
                for (auto [dest, symbol] : nfa.adj[src]) {
                    if (symbol == target_symbol)
                        dest_subset.insert(dest);
                }
            }

            if (dest_subset.empty())
                continue;

            usize& dest_subset_id = ids[dest_subset];

            /*
             *  If this subset has not been visited yet, give it an identifier
             *  and add it to the queue.
             */
            if (!dest_subset_id) {
                dest_subset_id = next_id++;
                queue.push(std::move(dest_subset));
            }

            /* Create the actual edge from the source subset to the destination */
            edges.push_back({src_subset_id, dest_subset_id, target_symbol});
        }
    }

    Graph dfa{};
    dfa.adj.resize(next_id - 1);
    dfa.flags.resize(next_id - 1);

    for (auto& e : edges)
        dfa.adj[e.src - 1].push_back({e.dest - 1, e.symbol});
    for (auto src : finals)
        dfa.flags[src - 1] |= FINAL;

    auto start_subset_id = first_id - 1;
    dfa.flags[start_subset_id] |= START;
    dfa.start = start_subset_id;

    return dfa;
}

void
print_components(const Graph& g, FILE* output)
{
    auto& [adj, flags, start] = g;

    auto size = adj.size();

    /* Print states */
    fprintf(output, "STATES: {q%lu", start);
    for (usize src = 0; src < size; ++src) {
        if (src == start)
            continue;

        fprintf(output, ", q%lu", src);
    }
    fprintf(output, "}\n");

    /* Print alphabet */
    std::set<char> sigma;
    for (usize src = 0; src < size; ++src) {
        for (auto& [_, symbol] : adj[src])
            sigma.insert(symbol);
    }

    fprintf(output, "SIGMA: {%c", *sigma.begin());
    for (auto it = std::next(sigma.begin()); it != sigma.end(); ++it)
        fprintf(output, ", %c", *it);
    fprintf(output, "}\n");

    /* Print transitions */
    fprintf(output, "TRANSITIONS:\n");
    for (usize src = 0; src < size; ++src) {
        for (auto [dest, symbol] : adj[src])
            fprintf(output, "\tδ(q%lu, %c) = q%lu\n", src, symbol, dest);
    }

    /* Print start state */
    fprintf(output, "START STATE: q%lu\n", g.start);

    /* Print final states */
    fprintf(output, "FINAL STATES: {");
    bool first = true;
    for (usize src = 0; src < size; ++src) {
        if (flags[src] & FINAL) {
            fprintf(output, first ? "q%lu" : ", q%lu", src);
            first = false;
        }
    }
    fprintf(output, "}\n");
}

void
set_attrs(void* obj, const AgobjAttrs& attrs)
{
    if (attrs.label)
        agsafeset(obj, (char*)"label", (char*)attrs.label, (char*)"");
    if (attrs.color)
        agsafeset(obj, (char*)"color", (char*)attrs.color, (char*)"");
    if (attrs.font)
        agsafeset(obj, (char*)"fontname", (char*)attrs.font, (char*)"");
    if (attrs.style)
        agsafeset(obj, (char*)"style", (char*)attrs.style, (char*)"");
    if (attrs.rankdir)
        agsafeset(obj, (char*)"rankdir", (char*)attrs.rankdir, (char*)"");
}

void
export_graph(const Graph& g, FILE* output, const std::string& reg)
{
    const auto& [adj, flags, _] = g;
    const usize size = adj.size();

    Agraph_t* graph = agopen((char*)"g", Agdirected, 0);
    assert(graph);
    set_attrs(graph, {.label = reg.data(), .font = FONT, .rankdir = "LR"});

    std::vector<Agnode_t*> g_nodes(size, nullptr);
    std::array<char, 4> lb = {};
    for (usize src = 0; src < size; ++src) {
        *std::to_chars(lb.data(), lb.data() + sizeof(lb) - 1, src).ptr = '\0';

        auto node = agnode(graph, lb.data(), 1);
        assert(node);
        g_nodes[src] = node;

        AgobjAttrs attrs;
        switch (flags[src] & (START | FINAL)) {
        case START | FINAL:
            attrs = {.style = "wedged", .font = FONT, .color = START_FINAL_COLOR};
            break;
        case START:
            attrs = {.style = "filled", .font = FONT, .color = START_COLOR};
            break;
        case FINAL:
            attrs = {.style = "filled", .font = FONT, .color = FINAL_COLOR};
            break;
        default:
            attrs = {.font = FONT};
            break;
        }

        set_attrs(node, attrs);
    }

    for (usize src = 0; src < size; ++src) {
        for (auto [dest, symbol] : adj[src]) {
            lb = {symbol};
            if (lb[0] == S_LAMBDA)
                lb = LAMBDA_UTF;

            auto edge = agedge(graph, g_nodes[src], g_nodes[dest], nullptr, 1);
            assert(edge);
            set_attrs(edge, {.label = lb.data(), .font = FONT});
        }
    }

    GVC_t* context = gvContext();
    assert(context);
    gvLayout(context, graph, "dot");
    gvRender(context, graph, "dot", output);

    gvFreeLayout(context, graph);
    agclose(graph);
}

void
usage()
{
    /* clang-format off */
    fprintf(
        stderr,
        "%s\n",
        "USAGE:\n"
        "    rtd [FLAGS/OPTIONS] <regex>\n\n"
        "FLAGS:\n"
        "    -h\n"
        "        Print help info.\n"
        "    -a\n"
        "        Set the alphabet of the regex as all alphanumericals.\n"
        "    -e\n"
        "        Export the graph in DOT language (by default, only the DFA components will be printed)\n\n"
        "OPTIONS:\n"
        "    -s <alphabet>\n"
        "        Set the alphabet of the regex (only alphanumericals allowed).\n"
        "    -o <output_file>\n"
        "        Set the path at which the graph file will be written (default is stdout).");
    /* clang-format on */
}

int
main(const int argc, char* argv[])
{
    const char* alphabet = DEFAULT_ALPHABET;
    const char* output_path = nullptr;
    bool all_alnum = false;
    bool exp = false;

    int opt;
    while ((opt = getopt(argc, argv, "heas:o:")) != -1) {
        switch (opt) {
        case 'h':
            usage();
            return EXIT_FAILURE;
        case 'e':
            exp = true;
            break;
        case 'a':
            all_alnum = true;
            break;
        case 's':
            alphabet = optarg;
            break;
        case 'o':
            output_path = optarg;
            break;
        default:
            usage();
            return EXIT_FAILURE;
        }
    }

    if (all_alnum)
        alphabet = ALL_ALPHANUMS;

    if (optind >= argc) {
        fprintf(stderr, "Missing <regex> argument\n\n");
        usage();
        return EXIT_FAILURE;
    }

    for (char c : std::string_view(alphabet)) {
        if (!std::isalnum(c)) {
            fprintf(stderr, "The alphabet can only contain alphanumericals\n");
            return EXIT_FAILURE;
        }

        in_alphabet[u8(c)] = true;
    }

    const std::string_view infix = argv[optind];
    const auto with_concat_op = add_concatenation_op(infix);
    const auto postfix = get_postfix(with_concat_op);
    if (!postfix) {
        fprintf(stderr, "Regex %s is invalid\n", infix.data());
        usage();
        return EXIT_FAILURE;
    }

#ifdef RTD_DEBUG
    fprintf(stderr,
            "Infix: %s\nInfix with explicit concatenation operator: %s\nPostfix: %s\n",
            infix.data(),
            with_concat_op.data(),
            postfix->data());
#endif

    auto root = get_nfa(*postfix);
    if (!root) {
        fprintf(stderr, "Failed to make NFA from regex\n");
        usage();
        return EXIT_FAILURE;
    }

    auto nfa_graph = to_graph(*root);

    /* No need for the old NFA representation anymore */
    for (NFANode* node : node_ptrs)
        delete node;

    /* Transform λ-NFA to NFA without λ-transitions */
    add_transitive_closure(nfa_graph);
    remove_lambdas(nfa_graph);

    auto dfa_graph = to_dfa_graph(nfa_graph);

    auto output = output_path ? fopen(output_path, "w") : stdout;
    if (!output) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    if (exp)
        export_graph(dfa_graph, output, "\n\n" + std::string(infix));
    else
        print_components(dfa_graph, output);
}
