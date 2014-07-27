/**
 * @file   test_config_lexer.c
 * @brief  Unit tests for the configuration file lexer.
 * @author Mikey Austin
 * @date   2014
 */

#include "test.h"
#include "../config_lexer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int
main()
{
    char *test_file;
    Config_source_T cs;
    Config_lexer_T lexer;
    int tok;

    TEST_START(63);

    /*
     * Test the string config source first.
     */
    cs = Config_source_create_from_str(
        "test_var_1    =  12345 # This is a comment \n"
        "# This is another comment followed by a new line \n"
        "section {\n"
        "    test_var_2 = \"long \\\"string\\\"\", # A \"quoted\" string literal\n"
        "    test_var_3 = 12\n"
        "} \n"
        "include \"/etc/somefile\"");
    lexer = Config_lexer_create(cs);
    TEST_OK((lexer != NULL), "Lexer created successfully");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_NAME), "Test name token type");
    TEST_OK(!strncmp(lexer->current_value.s, "test_var_1", strlen("test_var_1")), "Test token name value");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EQ), "Test equals token type");

    TEST_OK((lexer->previous_token == CONFIG_LEXER_TOK_NAME), "Test previous token type");
    TEST_OK(!strncmp(lexer->previous_value.s, "test_var_1", strlen("test_var_1")), "Test previous name value");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_INT), "Test int token type");
    TEST_OK((lexer->current_value.i == 12345), "Test token int value");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_SECTION), "Test section keyword");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_BRACKET_L), "Test left bracket");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_NAME), "Test name token type");
    TEST_OK(!strncmp(lexer->current_value.s, "test_var_2", strlen("test_var_2")), "Test token name value");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EQ), "Test equals token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_STR), "Test name string type");
    TEST_OK(!strncmp(lexer->current_value.s, "long \"string\"", strlen("long \"string\"")), "Test string value");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_COMMA), "Test comma token type");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_NAME), "Test name token type");
    TEST_OK(!strncmp(lexer->current_value.s, "test_var_3", strlen("test_var_3")), "Test token name value");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EQ), "Test equals token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_INT), "Test int token type");
    TEST_OK((lexer->current_value.i == 12), "Test token int value");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_BRACKET_R), "Test right bracket");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_INCLUDE), "Test include keyword");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_STR), "Test string type");
    TEST_OK(!strncmp(lexer->current_value.s, "/etc/somefile", strlen("/etc/somefile")), "Test string value");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOF), "Test EOF");

    Config_lexer_destroy(lexer);

    /*
     * Run the tests from the equivalent file source to be thourough.
     */
    test_file = Test_get_file_path("config_lexer_test1.conf");
    cs = Config_source_create_from_file(test_file);

    lexer = Config_lexer_create(cs);
    TEST_OK((lexer != NULL), "Lexer created successfully");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_NAME), "Test name token type");
    TEST_OK(!strncmp(lexer->current_value.s, "test_var_1", strlen("test_var_1")), "Test token name value");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EQ), "Test equals token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_INT), "Test int token type");
    TEST_OK((lexer->current_value.i == 12345), "Test token int value");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_SECTION), "Test section keyword");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_BRACKET_L), "Test left bracket");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_NAME), "Test name token type");
    TEST_OK(!strncmp(lexer->current_value.s, "test_var_2", strlen("test_var_2")), "Test token name value");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EQ), "Test equals token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_STR), "Test name string type");
    TEST_OK(!strncmp(lexer->current_value.s, "long \"string\"", strlen("long \"string\"")), "Test string value");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_BRACKET_R), "Test right bracket");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");
    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_INCLUDE), "Test include keyword");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_STR), "Test string type");
    TEST_OK(!strncmp(lexer->current_value.s, "/etc/somefile", strlen("/etc/somefile")), "Test string value");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOL), "Test EOL token type");

    TEST_OK(((tok = Config_lexer_next_token(lexer)) == CONFIG_LEXER_TOK_EOF), "Test EOF");

    Config_lexer_destroy(lexer);
    free(test_file);

    TEST_COMPLETE;
}
