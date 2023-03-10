#include <fmt/core.h>
#include <graphviz/gvc.h>
#include <array>
#include <vector>
#include <string>
#include <stack>
#include <queue>
#include <optional>
#include "numtypes.hpp"

/* Macros */
#define S_LAMBDA '\0'
#define OP_CONCAT '.'
#define OP_UNION '|'
#define OP_KLEENE '*'
#define NUM_CHARS (1 << 8)

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
static TokenType type_of(char);
static std::string add_concatenation_op(const std::string&);
static std::optional<std::string> get_postfix(const std::string&);
static std::optional<NFANode*> get_nfa(const std::string&);
static void get_nodes(NFANode*, std::vector<NFANode*>&);

/* Functions definitions  */
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
add_concatenation_op(const std::string& infix)
{
    if (infix.empty())
        return "";

    std::string result = infix.substr(0, 1);
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
get_postfix(const std::string& infix)
{
    /* Apply Dijkstra's 'shunting yard' algorithm */

    std::string postfix = "";
    std::stack<char> operators;
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
get_nfa(const std::string& postfix)
{
    std::stack<NFAFragment> nfa_components;
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
    nodes.push_back(root);

    get_nodes(root->neighbours[0], nodes);
    get_nodes(root->neighbours[1], nodes);
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

    const char* infix = argv[1];
    const auto with_concat_op = add_concatenation_op(infix);
    const auto postfix = get_postfix(with_concat_op);
    if (postfix) {
        fmt::print("Infix: {}\nWith explicit concat: {}\nPostfix: {}\n",
                   infix,
                   with_concat_op,
                   *postfix);
    } else {
        fmt::print(stderr, "Regex '{}' is invalid\n", infix);
        return EXIT_FAILURE;
    }

    auto root = get_nfa(*postfix);
    if (root) {
        get_nodes(*root, nodes);

        for (NFANode* node : nodes)
            delete node;
    } else {
        fmt::print(stderr, "Failed to make NFA from regex\n");
        return EXIT_FAILURE;
    }
}
