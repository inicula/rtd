#include <fmt/core.h>
#include <graphviz/gvc.h>
#include <array>
#include <vector>
#include <string>
#include <stack>
#include <queue>
#include <optional>
#include <charconv>
#include "numtypes.hpp"

/* Macros */
/* clang-format off */
#define S_LAMBDA   '\0'
#define OP_CONCAT  '.'
#define OP_UNION   '|'
#define OP_KLEENE  '*'
#define NUM_CHARS  (1 << 8)
#define LAMBDA_UTF {char(0xce), char(0xbb)}
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

/* Globals */
static bool in_alphabet[NUM_CHARS] = {};
static std::vector<NFANode*> nodes;
static constexpr auto OP_PREC = []() {
    std::array<u8, NUM_CHARS> arr = {};
    arr[OP_KLEENE] = 3;
    arr[OP_CONCAT] = 2;
    arr[OP_UNION] = 1;

    return arr;
}();

/* Functions declarations */
static void fmt_perror(const char*);
static TokenType type_of(char);
static std::string add_concatenation_op(const std::string_view);
static std::optional<std::string> get_postfix(const std::string_view);
static std::optional<NFANode*> get_nfa(const std::string_view);
static void get_nodes(NFANode*, std::vector<NFANode*>&);

/* Functions definitions  */
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
        if ((t_a == TokenType::REGULAR && t_b == TokenType::REGULAR) ||
            (t_a == TokenType::REGULAR && t_b == TokenType::LEFT_PAREN) ||
            (a == OP_KLEENE && t_b == TokenType::REGULAR) ||
            (a == OP_KLEENE && t_b == TokenType::LEFT_PAREN) ||
            (t_a == TokenType::RIGHT_PAREN && t_b == TokenType::REGULAR) ||
            (t_a == TokenType::RIGHT_PAREN && t_b == TokenType::LEFT_PAREN))
            result += OP_CONCAT;

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
                if (type_of(top) == TokenType::LEFT_PAREN)
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
            while (!operators.empty() && type_of(operators.top()) != TokenType::LEFT_PAREN) {
                postfix += operators.top();
                operators.pop();
            }

            if (operators.empty() || type_of(operators.top()) != TokenType::LEFT_PAREN)
                return std::nullopt;

            operators.pop();
            break;
        case TokenType::ERROR:
            return std::nullopt;
        }
    }

    while (!operators.empty()) {
        auto op = operators.top();
        if (type_of(op) == TokenType::LEFT_PAREN)
            return std::nullopt;

        postfix += op;
        operators.pop();
    }

    return postfix;
}

std::optional<NFANode*>
get_nfa(const std::string_view postfix)
{
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
        } else if (token == OP_KLEENE) {
            if (nfa_components.empty())
                return std::nullopt;

            auto x = nfa_components.top();
            nfa_components.pop();

            f = new NFANode{};
            q = new NFANode{{x.start, f}, {S_LAMBDA, S_LAMBDA}};
            *(x.finish) = {{x.start, f}, {S_LAMBDA, S_LAMBDA}};
        } else if (in_alphabet[u8(token)]) {
            f = new NFANode{};
            q = new NFANode{{f}, {token}};
        } else {
            return std::nullopt;
        }

        nfa_components.push({q, f});
    }

    if (nfa_components.empty())
        return std::nullopt;

    return nfa_components.top().start;
}

void
get_nodes(NFANode* root, std::vector<NFANode*>& nodes)
{
    if (!root || root->visited)
        return;

    root->visited = true;
    root->id = nodes.size();
    nodes.push_back(root);

    get_nodes(root->neighbours[0], nodes);
    get_nodes(root->neighbours[1], nodes);
}

void
make_graph(const char* path)
{
    GVC_t* gvc = gvContext();
    Agraph_t* g = agopen((char*)"g", Agdirected, 0);

    std::array<char, 8> label = {};
    std::vector<Agnode_t*> gvc_nodes(nodes.size(), nullptr);
    for (usize i = 0; i < nodes.size(); ++i) {
        *std::to_chars(label.data(), label.data() + sizeof(label) - 1, i).ptr = '\0';
        gvc_nodes[i] = agnode(g, label.data(), 1);
    }

    for (usize i = 0; i < nodes.size(); ++i) {
        NFANode* src = nodes[i];

        for (usize j = 0; j < std::size(src->neighbours); ++j) {
            NFANode* dest = src->neighbours[j];

            label = {src->symbols[j]};
            if (label[0] == S_LAMBDA)
                label = LAMBDA_UTF;

            if (dest) {
                auto e = agedge(g, gvc_nodes[src->id], gvc_nodes[dest->id], nullptr, 1);
                agsafeset(e, (char*)"label", label.data(), (char*)"");
            }
        }
    }

    auto file = fopen(path, "w");
    if (!file) {
        fmt_perror("fopen");
        return;
    }

    gvLayout(gvc, g, "dot");
    gvRender(gvc, g, "dot", file);

    gvFreeLayout(gvc, g);
    agclose(g);
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

    fmt::print("Infix: {}\nWith explicit concatenation operator: {}\nPostfix: {}\n",
               infix,
               with_concat_op,
               *postfix);

    auto root = get_nfa(*postfix);
    if (!root) {
        fmt::print(stderr, "Failed to make NFA from regex\n");
        return EXIT_FAILURE;
    }

    get_nodes(*root, nodes);
    make_graph("graph.dot");

    for (NFANode* node : nodes)
        delete node;
}
