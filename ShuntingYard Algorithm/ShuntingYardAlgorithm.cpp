
/***********************************************************************************************************************
 * Shunting Yard Algorithm
 *
 * To facilitate the addition of new operators and functions, 
 * let's adopt a structured and modular approach ::
 *
 * Use data structures to store information about operators and functions. 
 * This allows for easy additions.
 *
 * Use std::unordered_map to map operator/function names to their properties and implementations. 
 * This avoids lengthy if-else or switch statements.
 *
 * Encapsulate the logic for evaluating operators and functions in separate functions or classes.
 *
 ***********************************************************************************************************************
 *
 * Created : 2024-07-08
 * Author  : maxtarpini@gmail.com
 *
 * This work is released under the Creative Commons CC0 1.0 Universal Public Domain Dedication
 *
 **********************************************************************************************************************/

#include <iostream>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <functional>


// Type definition
//typedef float number;
typedef double number;


// Structures for Operators and Functions
enum class TokenType { NUMBER, OPERATOR, FUNCTION, PARENTHESIS, VARIABLE };

struct Token 
{
    TokenType type;
    std::string value;

    Token(TokenType type, const std::string& value) : type(type), value(value) {}
};


// Maps for Operators and Functions
struct Operator 
{
    int precedence;
    bool leftAssociative;
    std::function<number(number, number)> function;
};

struct Function {
    std::function<number(number)> function;
};

std::unordered_map<std::string, Operator> operators = 
{
    {"+", {1, true, [](number a, number b) { return a + b; }}},             // Addition:        precedence 1, left-associative
    {"-", {1, true, [](number a, number b) { return a - b; }}},             // Subtraction:     precedence 1, left-associative
    {"*", {2, true, [](number a, number b) { return a * b; }}},             // Multiplication:  precedence 2, left-associative
    {"/", {2, true, [](number a, number b) { return a / b; }}},             // Division:        precedence 2, left-associative
    {"^", {3, false, [](number a, number b) { return std::pow(a, b); }}}    // Exponentiation:  precedence 3, right-associative
};

std::unordered_map<std::string, Function> functions = 
{
    {"sin", {[](number a) { return std::sin(a); }}},
    {"cos", {[](number a) { return std::cos(a); }}},
    {"tan", {[](number a) { return std::tan(a); }}}
};


// Tokenize Input
std::vector<Token> tokenize(const std::string& expression) 
{
    std::vector<Token> tokens;
    std::string numberBuffer;
    std::string funcBuffer;
	
    for (char ch : expression) 
	{
        // std::isdigit is no good .. 
        if (isdigit(static_cast<unsigned char>(ch)) || ch == '.') {
            numberBuffer += ch;
        } else 
		{
            if (!numberBuffer.empty()) {
                tokens.emplace_back(TokenType::NUMBER, numberBuffer);
                numberBuffer.clear();
            }
			
            // std::isalpha is no good .. 
            if (isalpha(static_cast<unsigned char>(ch))) 
			{
                funcBuffer += ch;
            } else 
			{
                if (!funcBuffer.empty()) 
				{
                    if (functions.find(funcBuffer) != functions.end())
                        tokens.emplace_back(TokenType::FUNCTION, funcBuffer);
                    else if (funcBuffer == "x")
                        tokens.emplace_back(TokenType::VARIABLE, funcBuffer);
                    else
                        throw std::runtime_error("Unknown function or variable: " + funcBuffer);
                    
                    funcBuffer.clear();
                }
				
                if (ch == '(' || ch == ')')
                    tokens.emplace_back(TokenType::PARENTHESIS, std::string(1, ch));
                else if (operators.find(std::string(1, ch)) != operators.end()) 
                    tokens.emplace_back(TokenType::OPERATOR, std::string(1, ch));
                else if (!isspace(static_cast<unsigned char>(ch))) 
                    throw std::runtime_error(std::string("Invalid character in expression: ") + ch);
            }
        }
    }
	
    if (!numberBuffer.empty())
        tokens.emplace_back(TokenType::NUMBER, numberBuffer);
	
    if (!funcBuffer.empty()) 
	{
        if (functions.find(funcBuffer) != functions.end())
            tokens.emplace_back(TokenType::FUNCTION, funcBuffer);
        else if (funcBuffer == "x")
            tokens.emplace_back(TokenType::VARIABLE, funcBuffer);
        else
            throw std::runtime_error("Unknown function or variable: " + funcBuffer);
    }
    return tokens;
}


// Shunting Yard Algorithm
std::vector<Token> shuntingYard(const std::vector<Token>& tokens) 
{
    std::vector<Token> output;
    std::stack<Token> operatorsStack;

    for (const Token& token : tokens) 
	{
        switch (token.type) 
		{
            case TokenType::NUMBER:
            case TokenType::VARIABLE:
                output.push_back(token);
                break;
            case TokenType::FUNCTION:
                operatorsStack.push(token);
                break;
            case TokenType::OPERATOR:
                while (!operatorsStack.empty() && 
                       operatorsStack.top().type == TokenType::OPERATOR &&
                       ((operators[token.value].leftAssociative && operators[token.value].precedence <= operators[operatorsStack.top().value].precedence) ||
                        (!operators[token.value].leftAssociative && operators[token.value].precedence < operators[operatorsStack.top().value].precedence))) {
                    output.push_back(operatorsStack.top());
                    operatorsStack.pop();
                }
                operatorsStack.push(token);
                break;
            case TokenType::PARENTHESIS:
                if (token.value == "(") 
				{
                    operatorsStack.push(token);
                } else if (token.value == ")") 
				{
                    while (!operatorsStack.empty() && operatorsStack.top().value != "(") 
					{
                        output.push_back(operatorsStack.top());
                        operatorsStack.pop();
                    }
					
                    if (!operatorsStack.empty() && operatorsStack.top().value == "(")
                        operatorsStack.pop();
                    
                    if (!operatorsStack.empty() && operatorsStack.top().type == TokenType::FUNCTION) 
					{
                        output.push_back(operatorsStack.top());
                        operatorsStack.pop();
                    }
                }
                break;
        }
    }

    while (!operatorsStack.empty()) 
	{
        if (operatorsStack.top().type == TokenType::PARENTHESIS)
            throw std::runtime_error("Mismatched parentheses in expression.");
        
        output.push_back(operatorsStack.top());
        operatorsStack.pop();
    }

    return output;
}


// Evaluate RPN (Reverse Polish Notation)
number evaluateRPN(const std::vector<Token>& tokens, number x) 
{
    std::stack<number> values;

    for (const Token& token : tokens) 
	{
        if (token.type == TokenType::NUMBER) 
		{
            values.push(std::stod(token.value));
        } else if (token.type == TokenType::VARIABLE) 
		{
            values.push(x);
        } else if (token.type == TokenType::OPERATOR) 
		{
            if (values.size() < 2)
                throw std::runtime_error("Insufficient values in expression for operator: " + token.value);
			
            number right = values.top(); values.pop();
            number left = values.top(); values.pop();
            values.push(operators[token.value].function(left, right));
        } else if (token.type == TokenType::FUNCTION) 
		{
            if (values.empty())
                throw std::runtime_error("Insufficient values in expression for function: " + token.value);
            
            number value = values.top(); values.pop();
            values.push(functions[token.value].function(value));
        }
    }

    if (values.size() != 1)
        throw std::runtime_error("User input has too many values.");

    return values.top();
}


// Main Function
int main() 
{
    try {
        std::string expression;
        std::cout << "Enter an expression: ";
        std::getline(std::cin, expression);

        number x;
        std::cout << "Enter the value of x: ";
        std::cin >> x;

        std::vector<Token> tokens = tokenize(expression);
        std::vector<Token> rpn = shuntingYard(tokens);
        number result = evaluateRPN(rpn, x);

        std::cout << "Result: " << result << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
