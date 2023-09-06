/*
 * cmd_parser.h
 *
 *	Module to parse custom commands. The commands need to be provided as a pointer to null terminated char arrays:
 *
 * Created: 21.01.2020 08:50:12
 *  Author: manuel.martin
 */ 


#ifndef CMD_PARSER_H_
#define CMD_PARSER_H_

#include <stdbool.h>

#define CMD_PARSER_COMMAND_INVALID -1
#define CMD_PARSER_COUNT(COMMANDS) (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

typedef struct cmd_parser_command_t
{
	char const * name;
	char const * description;
	void(*command_fpt)(struct cmd_parser_command_t const * self, char const * arg, int len);
	void * p_context;
} cmd_parser_command_t;

int cmd_parser_parse(char const * input, int input_len, cmd_parser_command_t const * command_list, int command_count);
int cmd_parser_parse_and_set_context(char const * input, int input_len, cmd_parser_command_t * command_list, int command_count, void * p_context);
int cmd_parser_print_title(char * buffer, int buffer_size, cmd_parser_command_t const * command_list, int command_count, char const * title);
int cmd_parser_print_command(char * buffer, int buffer_size, cmd_parser_command_t const * command_list, int command_count, int index);
bool cmd_parser_arg_to_int(char const * arg, int len, int * result);

#endif /* CMD_PARSER_H_ */