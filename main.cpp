#include <fmt/core.h>
#include <array>
#include <vector>
#include <string>
#include <stack>
#include <queue>
#include "numtypes.hpp"

/* Macros */
#define S_LAMBDA '\0'
#define OP_CONCAT '.'
#define OP_OR '|'
#define OP_KLEENE '*'
#define NUM_CHARS (1 << 8)

/* Typedefs */
using NodeId = u8;

/* Enums */
enum class NodeType : u8 {
    START = 0,
    REGULAR,
    FINAL,
};

enum class TokenType : u8 {
    REGULAR = 0,
    OPERATOR,
    LEFT_PAREN,
    RIGHT_PAREN,
    ERROR,
};

/* Structs */
struct Node {
    NodeType type;
};

struct Transition {
    NodeId id;
    char symbol;
};

/* Globals */
static bool IN_ALPHABET[NUM_CHARS] = {};
static constexpr auto OP_PREC = []() {
    std::array<u8, NUM_CHARS> arr = {};
    arr[OP_KLEENE] = 3;
    arr[OP_CONCAT] = 2;
    arr[OP_OR] = 1;

    return arr;
}();
static std::vector<std::vector<Transition>> adj;

/* Functions declarations */
static TokenType type_of(char);
static std::string add_concatenation_op(const std::string&);
static std::pair<std::string, bool> get_postfix(const std::string&);

/* Functions definitions  */
TokenType
type_of(char token)
{
    auto token_idx = u8(token);

    if (IN_ALPHABET[token_idx])
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

std::pair<std::string, bool>
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
                return std::make_pair("", false);

            operators.pop();
            break;
        case TokenType::ERROR:
            return std::make_pair("", false);
        }
    }

    while (!operators.empty()) {
        auto op = operators.top();
        if (type_of(op) == TokenType::LEFT_PAREN)
            return std::make_pair("", false);

        postfix += op;
        operators.pop();
    }

    return std::make_pair(postfix, true);
}

int
main(const int argc, const char* argv[])
{
    /* TODO: Take as input a string to be validated with the DFA. */
    if (argc != 2) {
        fmt::print(stderr, "reg-to-nfa <regex>\n");
        return EXIT_FAILURE;
    }

    /*
     *  Assume the expression's alphabet consists of lowercase English letters.
     *  TODO: Accept arbitrary ASCII alphabet.
     */
    for (char i = 'a'; i != 'z'; ++i)
        IN_ALPHABET[usize(i)] = true;

    const char* infix = argv[1];
    const auto with_concat_op = add_concatenation_op(infix);
    const auto [postfix, ok] = get_postfix(with_concat_op);
    if (ok) {
        fmt::print("Infix: {}\nWith explicit concat: {}\nPostfix: {}\n",
                   infix,
                   with_concat_op,
                   postfix);
    } else {
        fmt::print("Regex '{}' is invalid\n", infix);
    }
}
