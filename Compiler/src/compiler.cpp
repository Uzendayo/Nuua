/**
 * |---------------|
 * | Nuua Compiler |
 * |---------------|
 *
 * Copyright 2019 Erik Campobadal <soc@erik.cat>
 * https://nuua.io
 */
#include "../include/compiler.hpp"
#include "../../Parser/include/parser.hpp"
#include "../../Logger/include/logger.hpp"

Memory *Compiler::get_current_memory()
{
    switch (this->current_memory) {
        case FUNCTIONS_MEMORY: { return &this->program.functions; }
        case CLASSES_MEMORY: { return &this->program.classes; }
        default: { return &this->program.program; }
    }
}

void Compiler::add_opcode(OpCode opcode)
{
    this->get_current_memory()->code.push_back(opcode);
    this->get_current_memory()->lines.push_back(this->current_line);
}

void Compiler::add_constant(Value value)
{
    this->add_opcode(OP_PUSH);
    this->add_constant_only(value);
}

Program Compiler::compile(const char *source)
{
    Parser parser;

    auto structure = parser.parse(source);

    logger->info("Started compiling...");

    for (auto node : structure) this->compile(node);
    this->add_opcode(OP_EXIT);

    #if DEBUG
        logger->info("Program memory:");
        this->program.program.dump();
        logger->info("Functions memory:");
        this->program.functions.dump();
        logger->info("Classes memory:");
        this->program.classes.dump();
    #endif

    logger->success("Compiling completed");

    return this->program;
}

void Compiler::compile(Statement *rule)
{
    this->current_line = rule->line;

    switch (rule->rule) {
        case RULE_EXPRESSION:
        case RULE_STATEMENT: {
            logger->error("Invalid rule to compile. Found Statement or expression without propper format.");
            exit(EXIT_FAILURE);
            break; // I won't remove this line to keep it understandable
        }
        case RULE_PRINT: {
            this->compile(static_cast<Print *>(rule)->expression);
            this->add_opcode(OP_PRINT);
            break;
        }
        case RULE_EXPRESSION_STATEMENT: {
            this->compile(static_cast<ExpressionStatement *>(rule)->expression);
            // The stack needs to be cleared afterwards since this is an expression
            // That's left alone. The stack needs to be popped.
            this->add_opcode(OP_POP);
            break;
        }
        case RULE_DECLARATION : {
            auto declaration = static_cast<Declaration *>(rule);

            this->add_opcode(OP_DECLARE);
            this->add_constant_only(declaration->name);
            this->add_constant_only(Type(declaration->type));
            // this->add_constant_only(Value(Type(declaration->type))); // This is the long version

            if (declaration->initializer) {
                this->compile(declaration->initializer);
                this->add_opcode(OP_STORE);
                this->add_constant_only(declaration->name);

                // Pop the value of the OP_PUSH since it's a statement.
                this->add_opcode(OP_POP);
            }

            break;
        }
        case RULE_RETURN: {
            auto ret = static_cast<Return *>(rule);
            this->compile(ret->value);
            this->add_opcode(OP_RETURN);
            break;
        }
        case RULE_IF: {
            //! Still needs to finish what happens if the else branch is present!!
            auto rif = static_cast<If *>(rule);
            if (rif->elseBranch.size() == 0) {
                this->compile(rif->condition);

                this->add_opcode(OP_BRANCH_FALSE);
                auto constant_index = this->add_constant_only(static_cast<int64_t>(0));
                auto start_index = this->current_code_line();

                for (auto stmt : rif->thenBranch) this->compile(stmt);

                this->modify_constant(constant_index, Value(static_cast<int64_t>(this->current_code_line() - start_index)));
            }
            break;
        }
        case RULE_WHILE: {
            auto rwhile = static_cast<While *>(rule);
            double initial_index = this->current_code_line();
            this->compile(rwhile->condition);

            this->add_opcode(OP_BRANCH_FALSE);
            auto constant_index = this->add_constant_only(static_cast<int64_t>(0));
            auto start_index = this->current_code_line();

            for (auto stmt : rwhile->body) this->compile(stmt);

            this->add_opcode(OP_RJUMP);
            this->add_constant_only(static_cast<int64_t>(-(this->current_code_line() - initial_index)));

            this->modify_constant(constant_index, Value(static_cast<int64_t>(this->current_code_line() - start_index + 1)));

            break;
        }
        default: {
            logger->error("Invalid statemetn to compile.", rule->line);
            exit(EXIT_FAILURE);
        }
    }
}

void Compiler::compile(Expression *rule)
{
    this->current_line = rule->line;

    switch (rule->rule) {
        case RULE_INTEGER: {
            this->add_constant(Value(static_cast<Integer *>(rule)->value));
            break;
        }
        case RULE_FLOAT: {
            this->add_constant(Value(static_cast<Float *>(rule)->value));
            break;
        }
        case RULE_STRING: {
            this->add_constant(Value(static_cast<String *>(rule)->value));
            break;
        }
        case RULE_BOOLEAN: {
            this->add_constant(Value(static_cast<Boolean *>(rule)->value));
            break;
        }
        case RULE_LIST: {
            auto list = static_cast<List *>(rule);
            for (int i = list->value.size() - 1; i >= 0; i--) this->compile(list->value.at(i));
            this->add_opcode(OP_LIST);
            this->add_constant_only(static_cast<double>(list->value.size()));
            break;
        }
        case RULE_DICTIONARY: {
            auto dictionary = static_cast<Dictionary *>(rule);
            for (int i = dictionary->key_order.size() - 1; i >= 0; i--) {
                this->add_constant(Value(dictionary->key_order[i]));
                this->compile(dictionary->value.at(dictionary->key_order[i]));
            }
            this->add_opcode(OP_DICTIONARY);
            this->add_constant_only(static_cast<double>(dictionary->value.size()));
            break;
        }
        case RULE_NONE: {
            this->add_constant(Value());
            break;
        }
        case RULE_GROUP: {
            this->compile(static_cast<Group *>(rule)->expression);
            break;
        }
        case RULE_UNARY: {
            auto unary = static_cast<Unary *>(rule);
            this->compile(unary->right);
            this->compile(unary->op, true);
            break;
        }
        case RULE_BINARY: {
            auto binary = static_cast<Binary *>(rule);
            this->compile(binary->left);
            this->compile(binary->right);
            this->compile(binary->op);
            break;
        }
        case RULE_VARIABLE: {
            this->add_opcode(OP_LOAD);
            this->add_constant_only(static_cast<Variable*>(rule)->name);
            break;
        }
        case RULE_ASSIGN: {
            auto assign = static_cast<Assign *>(rule);
            this->compile(assign->value);
            this->add_opcode(OP_STORE);
            this->add_constant_only(assign->name);
            break;
        }
        case RULE_ASSIGN_ACCESS: {
            auto assign_access = static_cast<AssignAccess *>(rule);
            this->compile(assign_access->value);
            this->compile(assign_access->index);
            this->add_opcode(OP_STORE_ACCESS);
            this->add_constant_only(assign_access->name);
            break;
        }
        case RULE_LOGICAL: {
            auto logical = static_cast<Logical *>(rule);
            this->compile(logical->left);
            this->compile(logical->right);
            this->compile(logical->op);
            break;
        }
        case RULE_FUNCTION: {
            auto function = static_cast<Function *>(rule);
            auto memory = this->current_memory;

            this->current_memory = FUNCTIONS_MEMORY;

            double index = this->current_code_line();

            // Compile the function arguments
            for (auto argument : function->arguments) this->compile(argument);

            for (int16_t i = function->arguments.size() - 1; i >= 0; i--) {
                this->add_opcode(OP_ONLY_STORE);
                this->add_constant_only(static_cast<Declaration *>(function->arguments[i])->name);
            }

            // Compile the function body
            for (auto stmt : function->body) this->compile(stmt);

            // Add the default return statement.
            // If a previous return has been hit, it will
            // never run. However, if no return was found
            // this is the return it will hit. It returns none
            this->add_constant(Value());
            this->add_opcode(OP_RETURN);

            this->current_memory = memory;

            this->add_opcode(OP_FUNCTION);
            this->add_constant_only(static_cast<int64_t>(index));
            this->add_constant_only(function->return_type);

            break;
        }
        case RULE_CALL: {
            auto call = static_cast<Call *>(rule);
            for (auto argument : call->arguments) this->compile(argument);
            this->add_opcode(OP_CALL);
            this->add_constant_only(call->callee);
            this->add_constant_only(static_cast<int64_t>(call->arguments.size()));
            break;
        }
        case RULE_ACCESS: {
            auto access = static_cast<Access *>(rule);
            this->compile(access->index);
            this->add_opcode(OP_ACCESS);
            this->add_constant_only(access->name);
            break;
        }
        default: {
            logger->error("Invalid expression to compile.", rule->line);
            exit(EXIT_FAILURE);
        }
    }
}

void Compiler::compile(Token op, bool unary)
{
    this->current_line = op.line;

    switch (op.type) {
        case TOKEN_PLUS: { this->add_opcode(OP_ADD); break; }
        case TOKEN_MINUS: { unary ? this->add_opcode(OP_MINUS) : this->add_opcode(OP_SUB); break; }
        case TOKEN_STAR: { this->add_opcode(OP_MUL); break; }
        case TOKEN_SLASH: { this->add_opcode(OP_DIV); break; }
        case TOKEN_BANG: { this->add_opcode(OP_NOT); break; }
        case TOKEN_EQUAL: { this->add_opcode(OP_STORE); break; }
        case TOKEN_EQUAL_EQUAL: { this->add_opcode(OP_EQ); break; }
        case TOKEN_BANG_EQUAL: { this->add_opcode(OP_NEQ); break; }
        case TOKEN_LOWER: { this->add_opcode(OP_LT); break; }
        case TOKEN_LOWER_EQUAL: { this->add_opcode(OP_LTE); break; }
        case TOKEN_HIGHER: { this->add_opcode(OP_HT); break; }
        case TOKEN_HIGHER_EQUAL: { this->add_opcode(OP_HTE); break; }
        default: {
            logger->error("Unknown operation token in a binary instruction", op.line);
            exit(EXIT_FAILURE);
        }
    }
}

uint64_t Compiler::add_constant_only(Value value)
{
    this->get_current_memory()->constants.push_back(value);

    uint64_t index = this->get_current_memory()->constants.size() - 1;
    this->get_current_memory()->code.push_back(index);

    return index;
}

void Compiler::modify_constant(uint64_t index, Value value)
{
    this->get_current_memory()->constants[index] = value;
}

uint32_t Compiler::current_code_line()
{
    return this->get_current_memory()->code.size();
}
