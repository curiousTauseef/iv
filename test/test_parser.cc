#include <gtest/gtest.h>
#include <string>
#include "space.h"
#include "ast_factory.h"
#include "ast_serializer.h"
#include "parser.h"

class AstFactory
  : public iv::core::Space<1>,
    public iv::core::ast::BasicAstFactory<AstFactory> { };

TEST(ParserCase, StringTest) {
  typedef iv::core::Parser<AstFactory, std::string> Parser;
  AstFactory factory;
  std::string src("var i = 20;");
  Parser parser(&factory, src);
  const Parser::FunctionLiteral* global = parser.ParseProgram();
  EXPECT_TRUE(global);
  iv::core::ast::AstSerializer<AstFactory> serializer;
  global->Accept(&serializer);
}