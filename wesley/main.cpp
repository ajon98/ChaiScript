#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>

#include <iostream>
#include <map>
#include <fstream>

#include "boxedcpp.hpp"
#include "bootstrap.hpp"
#include "bootstrap_stl.hpp"

#include "langkit_lexer.hpp"
#include "langkit_parser.hpp"

class TokenType { public: enum Type { File, Whitespace, Identifier, Integer, Operator, Parens_Open, Parens_Close,
    Square_Open, Square_Close, Curly_Open, Curly_Close, Comma, Quoted_String, Single_Quoted_String, Carriage_Return, Semicolon,
    Function_Def, Scoped_Block, Statement, Equation, Return, Expression, Term, Factor, Negate, Comment,
    Value, Fun_Call, Method_Call, Comparison, If_Block, While_Block, Boolean, Real_Number, Array_Call, Variable_Decl, Array_Init,
    For_Block, Prefix, Break }; };

const char *tokentype_to_string(int tokentype) {
    const char *token_types[] = {"File", "Whitespace", "Identifier", "Integer", "Operator", "Parens_Open", "Parens_Close",
        "Square_Open", "Square_Close", "Curly_Open", "Curly_Close", "Comma", "Quoted_String", "Single_Quoted_String", "Carriage_Return", "Semicolon",
        "Function_Def", "Scoped_Block", "Statement", "Equation", "Return", "Expression", "Term", "Factor", "Negate", "Comment",
        "Value", "Fun_Call", "Method_Call", "Comparison", "If_Block", "While_Block", "Boolean", "Real Number", "Array_Call", "Variable_Decl", "Array_Init",
        "For_Block", "Prefix", "Break" };

    return token_types[tokentype];
}

struct ParserError {
    std::string reason;
    TokenPtr location;

    ParserError(const std::string &why, const TokenPtr where) : reason(why), location(where){ }
};

struct EvalError {
    std::string reason;
    TokenPtr location;

    EvalError(const std::string &why, const TokenPtr where) : reason(why), location(where) { }
};

struct ReturnValue {
    Boxed_Value retval;
    TokenPtr location;

    ReturnValue(const Boxed_Value &return_value, const TokenPtr where) : retval(return_value), location(where) { }
};

struct BreakLoop {
    TokenPtr location;

    BreakLoop(const TokenPtr where) : location(where) { }
};

Boxed_Value eval_token(BoxedCPP_System &ss, TokenPtr node);
Boxed_Value evaluate_string(Lexer &lexer, Rule &parser, BoxedCPP_System &ss, const std::string &input, const char *filename);

void debug_print(TokenPtr token, std::string prepend) {
    std::cout << prepend << "Token: " << token->text << "(" << tokentype_to_string(token->identifier) << ") @ " << token->filename
        << ": ("  << token->start.line << ", " << token->start.column << ") to ("
        << token->end.line << ", " << token->end.column << ") " << std::endl;

    for (unsigned int i = 0; i < token->children.size(); ++i) {
        debug_print(token->children[i], prepend + "  ");
    }
}

void debug_print(std::vector<TokenPtr> &tokens) {
    for (unsigned int i = 0; i < tokens.size(); ++i) {
        debug_print(tokens[i], "");
    }
}

//A function that prints any string passed to it

template <typename T>
void print(const T &t)
{
    std::cout << t << std::endl;
}

template<> void print<bool>(const bool &t)
{
    if (t) {
        std::cout << "true" << std::endl;
    }
    else {
        std::cout << "false" << std::endl;
    }
}

std::string concat_string(const std::string &s1, const std::string &s2) {
    return s1+s2;
}

const Boxed_Value add_two(BoxedCPP_System &ss, const std::vector<Boxed_Value> &vals) {
    return dispatch(ss.get_function("+"), vals);
}

const Boxed_Value eval(Lexer &lexer, Rule &parser, BoxedCPP_System &ss, const std::vector<Boxed_Value> &vals) {
    std::string val;

    try {
        val = Cast_Helper<std::string &>()(vals[0]);
    }
    catch (std::exception &e) {
        throw EvalError("Can not evaluate string: " + val, TokenPtr());
    }
    catch (EvalError &ee) {
        throw EvalError("Can not evaluate string: " + val + " reason: " + ee.reason, TokenPtr());
    }
    return evaluate_string(lexer, parser, ss, val, "__EVAL__");
}

std::string load_file(const char *filename) {
    std::ifstream infile (filename, std::ios::in | std::ios::ate);

    if (!infile.is_open()) {
        std::cerr << "Can not open " << filename << std::endl;
        exit(0);
    }

    std::streampos size = infile.tellg();
    infile.seekg(0, std::ios::beg);

    std::vector<char> v(size);
    infile.read(&v[0], size);

    std::string ret_val (v.empty() ? std::string() : std::string (v.begin(), v.end()).c_str());

    return ret_val;
}

const Boxed_Value eval_function (BoxedCPP_System &ss, TokenPtr node, const std::vector<std::string> &param_names, const std::vector<Boxed_Value> &vals) {
    for (unsigned int i = 0; i < param_names.size(); ++i) {
        ss.add_object(param_names[i], vals[i]);
    }
    return eval_token(ss, node);
}

Lexer build_lexer() {
    Lexer lexer;
    lexer.set_skip(Pattern("[ \\t]+", TokenType::Whitespace));
    lexer.set_line_sep(Pattern("\\n|\\r\\n", TokenType::Carriage_Return));
    lexer.set_command_sep(Pattern(";|\\r\\n|\\n", TokenType::Semicolon));
    lexer.set_multiline_comment(Pattern("/\\*", TokenType::Comment), Pattern("\\*/", TokenType::Comment));
    lexer.set_singleline_comment(Pattern("//", TokenType::Comment));

    lexer << Pattern("[A-Za-z_]+", TokenType::Identifier);
    lexer << Pattern("[0-9]+\\.[0-9]+", TokenType::Real_Number);
    lexer << Pattern("[0-9]+", TokenType::Integer);
    lexer << Pattern("[!@#$%^&*|\\-+=<>.]+|/[!@#$%^&|\\-+=<>]*", TokenType::Operator);
    lexer << Pattern("\\(", TokenType::Parens_Open);
    lexer << Pattern("\\)", TokenType::Parens_Close);
    lexer << Pattern("\\[", TokenType::Square_Open);
    lexer << Pattern("\\]", TokenType::Square_Close);
    lexer << Pattern("\\{", TokenType::Curly_Open);
    lexer << Pattern("\\}", TokenType::Curly_Close);
    lexer << Pattern(",", TokenType::Comma);
    lexer << Pattern("\"(?:[^\"\\\\]|\\\\.)*\"", TokenType::Quoted_String);
    lexer << Pattern("'(?:[^'\\\\]|\\\\.)*'", TokenType::Single_Quoted_String);

    return lexer;
}

Rule build_parser_rules() {
    Rule params;
    Rule block(TokenType::Scoped_Block);
    Rule fundef(TokenType::Function_Def);
    Rule statement;
    Rule equation(TokenType::Equation);
    Rule boolean(TokenType::Boolean);
    Rule comparison(TokenType::Comparison);
    Rule expression(TokenType::Expression);
    Rule term(TokenType::Term);
    Rule factor(TokenType::Factor);
    Rule negate(TokenType::Negate);
    Rule prefix(TokenType::Prefix);

    Rule funcall(TokenType::Fun_Call);
    Rule methodcall(TokenType::Method_Call);
    Rule if_block(TokenType::If_Block);
    Rule while_block(TokenType::While_Block);
    Rule for_block(TokenType::For_Block);
    Rule arraycall(TokenType::Array_Call);
    Rule vardecl(TokenType::Variable_Decl);
    Rule arrayinit(TokenType::Array_Init);

    Rule return_statement(TokenType::Return);
    Rule break_statement(TokenType::Break);

    Rule value;
    Rule statements;
    Rule for_conditions;
    Rule source_elem;
    Rule source_elems;
    Rule statement_list;

    Rule rule = *(Ign(Id(TokenType::Semicolon))) >> source_elems >> *(Ign(Id(TokenType::Semicolon)));

    source_elems = source_elem >> *(+Ign(Id(TokenType::Semicolon)) >> source_elem);
    source_elem = fundef | statement;
    statement_list = statement >> *(+Ign(Id(TokenType::Semicolon)) >> statement);
    statement = if_block | while_block | for_block | equation;

    if_block = Ign(Str("if")) >> boolean >> block >> *(*Ign(Id(TokenType::Semicolon)) >> Str("elseif") >> boolean >> block) >> ~(*Ign(Id(TokenType::Semicolon)) >> Str("else") >> block);
    while_block = Ign(Str("while")) >> boolean >> block;
    for_block = Ign(Str("for")) >> for_conditions >> block;
    for_conditions = Ign(Id(TokenType::Parens_Open)) >> ~equation >> Ign(Str(";")) >> boolean >> Ign(Str(";")) >> equation >> Ign(Id(TokenType::Parens_Close));

    fundef = Ign(Str("def")) >> Id(TokenType::Identifier) >> ~(Ign(Id(TokenType::Parens_Open)) >> ~params >> Ign(Id(TokenType::Parens_Close))) >>
        block;
    params = Id(TokenType::Identifier) >> *(Ign(Str(",")) >> Id(TokenType::Identifier));
    block = *(Ign(Id(TokenType::Semicolon))) >> Ign(Id(TokenType::Curly_Open)) >> *(Ign(Id(TokenType::Semicolon))) >> ~statement_list >> *(Ign(Id(TokenType::Semicolon))) >> Ign(Id(TokenType::Curly_Close));
    equation = *(((vardecl | arraycall | Id(TokenType::Identifier)) >> Str("=")) |
            ((vardecl | arraycall | Id(TokenType::Identifier)) >> Str("+=")) |
            ((vardecl | arraycall | Id(TokenType::Identifier)) >> Str("-=")) |
            ((vardecl | arraycall | Id(TokenType::Identifier)) >> Str("*=")) |
            ((vardecl | arraycall | Id(TokenType::Identifier)) >> Str("/="))) >> boolean;
    boolean = comparison >> *((Str("&&") >> comparison) | (Str("||") >> comparison));
    comparison = expression >> *((Str("==") >> expression) | (Str("!=") >> expression) | (Str("<") >> expression) |
            (Str("<=") >> expression) |(Str(">") >> expression) | (Str(">=") >> expression));
    expression = term >> *((Str("+") >> term) | (Str("-") >> term));
    term = factor >> *((Str("*") >> factor) | (Str("/") >> factor));
    factor = methodcall | arraycall | value | negate | prefix | (Ign(Str("+")) >> value);
    funcall = Id(TokenType::Identifier) >> Ign(Id(TokenType::Parens_Open)) >> ~(boolean >> *(Ign(Str("," )) >> boolean)) >> Ign(Id(TokenType::Parens_Close));
    methodcall = value >> +(Ign(Str(".")) >> funcall);
    negate = Ign(Str("-")) >> boolean;
    prefix = (Str("++") >> (boolean | arraycall)) | (Str("--") >> (boolean | arraycall));
    arraycall = value >> +((Ign(Id(TokenType::Square_Open)) >> boolean >> Ign(Id(TokenType::Square_Close))));
    value =  vardecl | arrayinit | block | (Ign(Id(TokenType::Parens_Open)) >> boolean >> Ign(Id(TokenType::Parens_Close))) | return_statement | break_statement |
        funcall | Id(TokenType::Identifier) | Id(TokenType::Real_Number) | Id(TokenType::Integer) | Id(TokenType::Quoted_String) |
        Id(TokenType::Single_Quoted_String) ;
    arrayinit = Ign(Id(TokenType::Square_Open)) >> ~(boolean >> *(Ign(Str(",")) >> boolean))  >> Ign(Id(TokenType::Square_Close));
    vardecl = Ign(Str("var")) >> Id(TokenType::Identifier);
    return_statement = Ign(Str("return")) >> ~boolean;
    break_statement = Wrap(Ign(Str("break")));

    return rule;
}


BoxedCPP_System build_eval_system(Lexer &lexer, Rule &parser) {
    BoxedCPP_System ss;
    bootstrap(ss);
    bootstrap_vector<std::vector<int> >(ss, "VectorInt");
    bootstrap_vector<std::vector<Boxed_Value> >(ss, "Vector");
//    dump_system(ss);

    //Register a new function, this one with typing for us, so we don't have to ubox anything
    //right here
    register_function(ss, &print<bool>, "print");
    register_function(ss, &print<std::string>, "print");
    register_function(ss, &print<double>, "print");
    register_function(ss, &print<size_t>, "print");
    register_function(ss, &concat_string, "concat_string");
    register_function(ss, &print<int>, "print");

    ss.register_function(boost::function<void ()>(boost::bind(&dump_system, boost::ref(ss))), "dump_system");
    ss.register_function(boost::function<void (Boxed_Value)>(boost::bind(&dump_object, _1)), "dump_object");


    ss.register_function(boost::shared_ptr<Proxy_Function>(
          new Dynamic_Proxy_Function(boost::bind(&add_two, boost::ref(ss), _1), 2)), "add_two");

    ss.register_function(boost::shared_ptr<Proxy_Function>(
          new Dynamic_Proxy_Function(boost::bind(&eval, boost::ref(lexer), boost::ref(parser),
                  boost::ref(ss), _1), 1)), "eval");


    return ss;
}

Boxed_Value eval_token(BoxedCPP_System &ss, TokenPtr node) {
    Boxed_Value retval;
    unsigned int i, j;

    switch (node->identifier) {
        case (TokenType::Value) :
        case (TokenType::File) :
            for (i = 0; i < node->children.size(); ++i) {
                retval = eval_token(ss, node->children[i]);
            }
        break;
        case (TokenType::Identifier) :
            if (node->text == "true") {
                retval = Boxed_Value(true);
            }
            else if (node->text == "false") {
                retval = Boxed_Value(false);
            }
            else {
                try {
                    retval = ss.get_object(node->text);
                }
                catch (std::exception &e) {
                    throw EvalError("Can not find object: " + node->text, node);
                }
            }
        break;
        case (TokenType::Real_Number) :
            retval = Boxed_Value(double(atof(node->text.c_str())));
        break;
        case (TokenType::Integer) :
            retval = Boxed_Value(atoi(node->text.c_str()));
        break;
        case (TokenType::Quoted_String) :
            retval = Boxed_Value(node->text);
        break;
        case (TokenType::Single_Quoted_String) :
            retval = Boxed_Value(node->text);
        break;
        case (TokenType::Equation) :
            retval = eval_token(ss, node->children.back());
            if (node->children.size() > 1) {
                for (i = node->children.size()-3; ((int)i) >= 0; i -= 2) {
                    Param_List_Builder plb;
                    plb << eval_token(ss, node->children[i]);
                    plb << retval;
                    try {
                        retval = dispatch(ss.get_function(node->children[i+1]->text), plb);
                    }
                    catch(std::exception &e){
                        throw EvalError("Can not find appropriate '" + node->children[i+1]->text + "'", node->children[i+1]);
                    }
                }
            }
        break;
        case (TokenType::Variable_Decl): {
            ss.set_object(node->children[0]->text, Boxed_Value());
            retval = ss.get_object(node->children[0]->text);
        }
        break;
        case (TokenType::Factor) :
        case (TokenType::Expression) :
        case (TokenType::Term) :
        case (TokenType::Boolean) :
        case (TokenType::Comparison) : {
            retval = eval_token(ss, node->children[0]);
            if (node->children.size() > 1) {
                for (i = 1; i < node->children.size(); i += 2) {
                    Param_List_Builder plb;
                    plb << retval;
                    plb << eval_token(ss, node->children[i + 1]);

                    try {
                        retval = dispatch(ss.get_function(node->children[i]->text), plb);
                    }
                    catch(std::exception &e){
                        throw EvalError("Can not find appropriate '" + node->children[i]->text + "'", node->children[i]);
                    }
                }
            }
        }
        break;
        case (TokenType::Array_Call) : {
            retval = eval_token(ss, node->children[0]);
            for (i = 1; i < node->children.size(); ++i) {
                Param_List_Builder plb;
                plb << retval;
                plb << eval_token(ss, node->children[i]);
                try {
                    retval = dispatch(ss.get_function("[]"), plb);
                }
                catch(std::exception &e){
                    throw EvalError("Can not find appropriate array lookup '[]'", node->children[i]);
                }
            }
        }
        break;
        case (TokenType::Negate) : {
            retval = eval_token(ss, node->children[0]);
            Param_List_Builder plb;
            plb << retval;
            //plb << Boxed_Value(-1);

            try {
                retval = dispatch(ss.get_function("-"), plb);
            }
            catch(std::exception &e){
                throw EvalError("Can not find appropriate negation", node->children[0]);
            }
        }
        break;
        case (TokenType::Prefix) : {
            retval = eval_token(ss, node->children[1]);
            Param_List_Builder plb;
            plb << retval;

            try {
                retval = dispatch(ss.get_function(node->children[0]->text), plb);
            }
            catch(std::exception &e){
                throw EvalError("Can not find appropriate prefix", node->children[0]);
            }
        }
        break;
        case (TokenType::Array_Init) : {
            try {
                retval = dispatch(ss.get_function("Vector"), Param_List_Builder());
                for (i = 0; i < node->children.size(); ++i) {
                    try {
                        Boxed_Value tmp = eval_token(ss, node->children[i]);
                        dispatch(ss.get_function("push_back"), Param_List_Builder() << retval << tmp);
                    }
                    catch (std::exception inner_e) {
                        throw EvalError("Can not find appropriate 'push_back'", node->children[i]);
                    }
                }
            }
            catch (std::exception e) {
                throw EvalError("Can not find appropriate 'Vector()'", node);
            }
        }
        break;
        case (TokenType::Fun_Call) : {
            Param_List_Builder plb;
            for (i = 1; i < node->children.size(); ++i) {
                plb << eval_token(ss, node->children[i]);
            }
            try {
                retval = dispatch(ss.get_function(node->children[0]->text), plb);
            }
            catch(EvalError &ee) {
                throw EvalError(ee.reason, node->children[0]);
            }
            catch(std::exception &e){
                throw EvalError("Can not find appropriate '" + node->children[0]->text + "'", node->children[0]);
            }
            catch(ReturnValue &rv) {
                retval = rv.retval;
            }
        }
        break;
        case (TokenType::Method_Call) : {
            retval = eval_token(ss, node->children[0]);
            if (node->children.size() > 1) {
                for (i = 1; i < node->children.size(); ++i) {
                    Param_List_Builder plb;
                    plb << retval;

                    for (j = 1; j < node->children[i]->children.size(); ++j) {
                        plb << eval_token(ss, node->children[i]->children[j]);
                    }

                    try {
                        retval = dispatch(ss.get_function(node->children[i]->children[0]->text), plb);
                    }
                    catch(EvalError &ee) {
                        throw EvalError(ee.reason, node->children[0]);
                    }
                    catch(std::exception &e){
                        throw EvalError("Can not find appropriate '" + node->children[i]->children[0]->text + "'", node->children[0]);
                    }
                    catch(ReturnValue &rv) {
                        retval = rv.retval;
                    }
                }
            }
        }
        break;
        case(TokenType::If_Block) : {
            retval = eval_token(ss, node->children[0]);
            bool cond;
            try {
                cond = Cast_Helper<bool &>()(retval);
            }
            catch (std::exception &e) {
                throw EvalError("If condition not boolean", node->children[0]);
            }
            if (cond) {
                retval = eval_token(ss, node->children[1]);
            }
            else {
                if (node->children.size() > 2) {
                    i = 2;
                    while ((!cond) && (i < node->children.size())) {
                        if (node->children[i]->text == "else") {
                            retval = eval_token(ss, node->children[i+1]);
                            cond = true;
                        }
                        else if (node->children[i]->text == "elseif") {
                            retval = eval_token(ss, node->children[i+1]);
                            try {
                                cond = Cast_Helper<bool &>()(retval);
                            }
                            catch (std::exception &e) {
                                throw EvalError("Elseif condition not boolean", node->children[i+1]);
                            }
                            if (cond) {
                                retval = eval_token(ss, node->children[i+2]);
                            }
                        }
                        i = i + 3;
                    }
                }
            }
        }
        break;
        case(TokenType::While_Block) : {
            retval = eval_token(ss, node->children[0]);
            bool cond;
            try {
                cond = Cast_Helper<bool &>()(retval);
            }
            catch (std::exception) {
                throw EvalError("While condition not boolean", node->children[0]);
            }
            while (cond) {
                try {
                    eval_token(ss, node->children[1]);
                    retval = eval_token(ss, node->children[0]);
                    try {
                        cond = Cast_Helper<bool &>()(retval);
                    }
                    catch (std::exception) {
                        throw EvalError("While condition not boolean", node->children[0]);
                    }
                }
                catch (BreakLoop &bl) {
                    cond = false;
                }
            }
            retval = Boxed_Value();
        }
        break;
        case(TokenType::For_Block) : {
            Boxed_Value condition;
            bool cond;

            try {
                if (node->children.size() == 4) {
                    eval_token(ss, node->children[0]);
                    condition = eval_token(ss, node->children[1]);
                }
                else if (node->children.size() == 3){
                    condition = eval_token(ss, node->children[0]);
                }
                cond = Cast_Helper<bool &>()(condition);
            }
            catch (std::exception &e) {
                throw EvalError("For condition not boolean", node);
            }
            while (cond) {
                try {
                    if (node->children.size() == 4) {
                        eval_token(ss, node->children[3]);
                        eval_token(ss, node->children[2]);
                        condition = eval_token(ss, node->children[1]);
                    }
                    else if (node->children.size() == 3) {
                        eval_token(ss, node->children[2]);
                        eval_token(ss, node->children[1]);
                        condition = eval_token(ss, node->children[0]);
                    }
                    cond = Cast_Helper<bool &>()(condition);

                }
                catch (std::exception &e) {
                    throw EvalError("For condition not boolean", node);
                }
                catch (BreakLoop &bl) {
                    cond = false;
                }
            }
            retval = Boxed_Value();
        }
        break;
        case (TokenType::Function_Def) : {
            unsigned int num_args = node->children.size() - 2;
            std::vector<std::string> param_names;
            for (i = 0; i < num_args; ++i) {
                param_names.push_back(node->children[i+1]->text);
            }

            ss.register_function(boost::shared_ptr<Proxy_Function>(
                  new Dynamic_Proxy_Function(boost::bind(&eval_function, boost::ref(ss), node->children.back(), param_names, _1))), node->children[0]->text);
        }
        break;
        case (TokenType::Scoped_Block) : {
            ss.new_scope();
            for (i = 0; i < node->children.size(); ++i) {
                retval = eval_token(ss, node->children[i]);
            }
            ss.pop_scope();
        }
        break;
        case (TokenType::Return) : {
            if (node->children.size() > 0) {
                retval = eval_token(ss, node->children[0]);
            }
            else {
                retval = Boxed_Value();
            }
            throw ReturnValue(retval, node);
        }
        break;
        case (TokenType::Break) : {
            throw BreakLoop(node);
        }
        break;
        case (TokenType::Statement) :
        case (TokenType::Carriage_Return) :
        case (TokenType::Semicolon) :
        case (TokenType::Comment) :
        case (TokenType::Operator) :
        case (TokenType::Whitespace) :
        case (TokenType::Parens_Open) :
        case (TokenType::Parens_Close) :
        case (TokenType::Square_Open) :
        case (TokenType::Square_Close) :
        case (TokenType::Curly_Open) :
        case (TokenType::Curly_Close) :
        case (TokenType::Comma) :
        break;
    }

    return retval;
}


TokenPtr parse(Rule &rule, std::vector<TokenPtr> &tokens, const char *filename) {

    Token_Iterator iter = tokens.begin(), end = tokens.end();
    TokenPtr parent(new Token("Root", TokenType::File, filename));

    std::pair<Token_Iterator, bool> results = rule(iter, end, parent);

    if ((results.second) && (results.first == end)) {
        //debug_print(parent, "");
        return parent;
    }
    else {
        throw ParserError("Parse failed to complete", *(results.first));
        //throw ParserError("Parse failed to complete at: " + (*(results.first))->text , *(results.first));
    }
}

Boxed_Value evaluate_string(Lexer &lexer, Rule &parser, BoxedCPP_System &ss, const std::string &input, const char *filename) {
    std::vector<TokenPtr> tokens = lexer.lex(input, filename);
    Boxed_Value value;

    for (unsigned int i = 0; i < tokens.size(); ++i) {
        if ((tokens[i]->identifier == TokenType::Quoted_String) || (tokens[i]->identifier == TokenType::Single_Quoted_String)) {
            tokens[i]->text = tokens[i]->text.substr(1, tokens[i]->text.size()-2);
        }
    }

    //debug_print(tokens);
    try {
        TokenPtr parent = parse(parser, tokens, filename);
        value = eval_token(ss, parent);
    }
    catch (ParserError &pe) {
        if (filename != std::string("__EVAL__")) {
            std::cout << "Parsing error: \"" << pe.reason << "\" in '" << pe.location->filename << "' line: " << pe.location->start.line+1 << std::endl;
        }
        else {
            std::cout << "Parsing error: \"" << pe.reason << "\"" << std::endl;
        }
    }
    catch (EvalError &ee) {
        if (filename != std::string("__EVAL__")) {
            std::cout << "Eval error: \"" << ee.reason << "\" in '" << ee.location->filename << "' line: " << ee.location->start.line+1 << std::endl;
        }
        else {
            std::cout << "Eval error: \"" << ee.reason << "\"" << std::endl;
        }
    }
    catch (std::exception &e) {
        std::cout << "Exception: " << e.what() << std::endl;
    }

    return value;
}

int main(int argc, char *argv[]) {
    std::string input;

    Lexer lexer = build_lexer();
    Rule parser = build_parser_rules();
    BoxedCPP_System ss = build_eval_system(lexer, parser);

    if (argc < 2) {
        std::cout << "eval> ";
        std::getline(std::cin, input);
        while (input != "quit") {
            Boxed_Value val;
            try {
                val = evaluate_string(lexer, parser, ss, input, "__EVAL__");
            }
            catch (const ReturnValue &rv) {
                val = rv.retval;
            }
            if (val.get_type_info().m_bare_type_info && *(val.get_type_info().m_bare_type_info) != typeid(void)) {
                try {
                    Boxed_Value printeval = dispatch(ss.get_function("to_string"), Param_List_Builder() << val);
                    std::cout << "result: ";
                    dispatch(ss.get_function("print"), Param_List_Builder() << printeval);
                } catch (const std::runtime_error &e) {
                    //std::cout << "unhandled type: " << val.get_type_info().m_type_info->name() << std::endl;
                }
            }
            std::cout << "eval> ";
            std::getline(std::cin, input);
        }
    }
    else {
        for (int i = 1; i < argc; ++i) {
            Boxed_Value val = evaluate_string(lexer, parser, ss, load_file(argv[i]), argv[i]);
        }
    }
}

