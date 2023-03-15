#include <fmt/core.h>
#include <graphviz/gvc.h>
#include <array>
#include <vector>
#include <string>
#include <stack>
#include <algorithm>
#include <optional>
#include <charconv>
#include <cassert>
#include "numtypes.hpp"

/* Macros */
/* clang-format off */
#define START_NODE_COLOR         "turquoise"
#define FINAL_NODE_COLOR         "x11green"
#define START_FINAL_NODE_COLOR   "turquoise:x11green"
#define FONT                     "monospace"
#define S_LAMBDA                 '\0'
#define OP_CONCAT                '.'
#define OP_UNION                 '|'
#define OP_KLEENE                '*'
#define OP_PLUS                  '+'
#define OP_OPT                   '?'
#define NUM_CHARS                (1 << 8)
#define LAMBDA_UTF               {char(0xce), char(0xbb)}
/* clang-format on */

/* Enums */
enum class TokenType : u8 {
    REGULAR = 0,
    OPERATOR,
    LEFT_PAREN,
    RIGHT_PAREN,
    ERROR,
};

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

struct Transition {
    usize dest;
    char symbol;
};

struct AgobjAttrs {
    const char* label = nullptr;
    const char* style = nullptr;
    const char* font = nullptr;
    const char* color = nullptr;
};

/* Globals */
static bool in_alphabet[NUM_CHARS] = {};
static std::vector<NFANode*> node_ptrs;
static std::vector<std::vector<Transition>> adj;
static std::vector<u8> visited;
static std::vector<u8> is_final;  /* final[i] <=> node `i` represents a final state */
static std::vector<u8> is_active; /* active[i] <=> node `i` is not DEAD and not UNREACHABLE */
static u8 start_node;
static usize num_nodes;
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
static void fmt_perror(const char*);
static TokenType type_of(char);
static std::string add_concatenation_op(const std::string_view);
static std::optional<std::string> get_postfix(const std::string_view);
static std::optional<NFANode*> get_nfa(const std::string_view);
static void fill_adj_list(NFANode*);
static void transitive_closure_helper(usize, usize, std::vector<Transition>&);
static void transitive_closure();
static void remove_lambdas();
static void mark_active_helper(usize);
static void mark_active();
static void set_attrs(void*, const AgobjAttrs&);
static void make_graph(const char*, const std::string&);

/* Functions definitions  */
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

void
fmt_perror(const char* why)
{
    fmt::print(stderr, "{}: {}\n", why, strerror(errno));
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
fill_adj_list(NFANode* src)
{
    if (!src || src->visited)
        return;

    node_ptrs.push_back(src);

    src->visited = true;
    src->id = num_nodes++;
    while (src->id >= adj.size()) {
        adj.push_back({});
        is_final.push_back({});
    }

    auto v0 = src->neighbours[0];
    auto v1 = src->neighbours[1];

    is_final[src->id] = !v0 && !v1;

    if (v0) {
        fill_adj_list(v0);
        adj[src->id].push_back({v0->id, src->symbols[0]});
    }

    if (v1) {
        fill_adj_list(v1);
        adj[src->id].push_back({v1->id, src->symbols[1]});
    }
}

void
transitive_closure_helper(usize from, usize src, std::vector<Transition>& to_add)
{
    if (visited[src])
        return;

    visited[src] = true;

    for (auto [dest, symbol] : adj[src]) {
        if (symbol == S_LAMBDA) {
            to_add.push_back({dest, symbol});
            if (is_final[dest])
                is_final[from] = true;

            transitive_closure_helper(from, dest, to_add);
        }
    }
}

void
transitive_closure()
{
    std::vector<Transition> to_add;
    for (usize src = 0; src < adj.size(); ++src) {
        to_add.clear();
        std::fill(visited.begin(), visited.end(), false);
        transitive_closure_helper(src, src, to_add);

        adj[src].insert(adj[src].end(), to_add.begin(), to_add.end());
    }
}

void
remove_lambdas()
{
    for (usize u = 0; u < num_nodes; ++u) {
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

    for (auto& node_transitions : adj) {
        node_transitions.erase(std::partition(node_transitions.begin(),
                                              node_transitions.end(),
                                              [](auto& t) { return t.symbol != S_LAMBDA; }),
                               node_transitions.end());
        std::sort(node_transitions.begin(), node_transitions.end());
        node_transitions.erase(std::unique(node_transitions.begin(), node_transitions.end()),
                               node_transitions.end());
    }
}

void
mark_active_helper(usize src)
{
    if (visited[src])
        return;

    visited[src] = true;

    if (is_final[src])
        is_active[src] |= 1;

    for (auto [dest, symbol] : adj[src]) {
        mark_active_helper(dest);
        is_active[src] |= is_active[dest];
    }
}

void
mark_active()
{
    std::fill(visited.begin(), visited.end(), false);
    std::fill(is_active.begin(), is_active.end(), false);
    mark_active_helper(start_node);
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
}

void
make_graph(const char* path, const std::string& reg)
{
    Agraph_t* graph = agopen((char*)"g", Agdirected, 0);
    assert(graph);
    set_attrs(graph, {.label = reg.data(), .font = FONT});

    std::vector<Agnode_t*> g_nodes(num_nodes, nullptr);
    std::array<char, 4> lb = {};
    usize id = 0; /* Renumber states since we're ignoring dead ones */
    for (usize src = 0; src < num_nodes; ++src) {
        if (is_active[src]) {
            *std::to_chars(lb.data(), lb.data() + sizeof(lb) - 1, id++ + 1).ptr = '\0';
            g_nodes[src] = agnode(graph, lb.data(), 1);
            assert(g_nodes[src]);
            set_attrs(g_nodes[src], {.font = FONT});
        }
    }

    for (usize src = 0; src < num_nodes; ++src) {
        for (auto [dest, symbol] : adj[src]) {
            if (!is_active[src] || !is_active[dest])
                continue;

            lb = {symbol};
            if (lb[0] == S_LAMBDA)
                lb = LAMBDA_UTF;

            auto edge = agedge(graph, g_nodes[src], g_nodes[dest], nullptr, 1);
            assert(edge);
            set_attrs(edge, {.label = lb.data(), .font = FONT});
        }
    }

    if (!is_final[start_node])
        set_attrs(g_nodes[start_node], {.style = "filled", .color = START_NODE_COLOR});
    else
        set_attrs(g_nodes[start_node], {.style = "wedged", .color = START_FINAL_NODE_COLOR});

    for (usize src = 1; src < num_nodes; ++src) {
        if (is_active[src] && is_final[src])
            set_attrs(g_nodes[src], {.style = "filled", .color = FINAL_NODE_COLOR});
    }

    auto file = fopen(path, "w");
    if (!file) {
        fmt_perror("fopen");
        return;
    }

    GVC_t* context = gvContext();
    assert(context);
    gvLayout(context, graph, "dot");
    gvRender(context, graph, "dot", file);

    gvFreeLayout(context, graph);
    agclose(graph);
    fclose(file);
}

int
main(const int argc, const char* argv[])
{
    /* TODO: Take as input a string to be validated with the DFA. */
    if (argc != 2) {
        fmt::print(stderr, "Usage: rtd <regex>\n");
        return EXIT_FAILURE;
    }

    /*
     *  Assume the expression's alphabet consists of lowercase English letters.
     *  TODO: Accept arbitrary ASCII alphabet.
     */
    for (char i = 'a'; i <= 'z'; ++i)
        in_alphabet[u8(i)] = true;

    const std::string_view infix = argv[1];
    const auto with_concat_op = add_concatenation_op(infix);
    const auto postfix = get_postfix(with_concat_op);
    if (!postfix) {
        fmt::print(stderr, "Regex '{}' is invalid\n", infix);
        return EXIT_FAILURE;
    }

    fmt::print("Infix: {}\nInfix with explicit concatenation operator: {}\nPostfix: {}\n",
               infix,
               with_concat_op,
               *postfix);

    auto root = get_nfa(*postfix);
    if (!root) {
        fmt::print(stderr, "Failed to make NFA from regex\n");
        return EXIT_FAILURE;
    }

    /* Fill the adjacency  matrix and save node ptrs */
    fill_adj_list(*root);

    /* Initialize some helper vectors for when they're used */
    visited.resize(num_nodes);
    is_active.resize(num_nodes, true);

    /* No need for the old NFA representation anymore */
    for (NFANode* node : node_ptrs)
        delete node;

    /* Transform λ-NFA to NFA without λ-transitions and mark active states */
    transitive_closure();
    remove_lambdas();
    mark_active();

    make_graph("graph.dot", "\n\n" + std::string(infix));
}
