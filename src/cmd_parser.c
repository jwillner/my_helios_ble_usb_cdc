/*
 * command_parser.c
 *
 * Created: 21.01.2020 08:49:50
 *  Author: manuel.martin
 */ 

#include "cmd_parser.h"

#include <stddef.h>
#include <string.h>
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// type definitions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct print_line_values_s
{
	int command_len;
	int description_len;
	int complete_len;
} print_line_values_t;
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// static function declarations
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int count_whitespaces(char const * arg, int len);
static bool is_whitespace(char c);
static bool has_suffix(char const * arg, int len, char const * suffix, int suffix_len);
static bool hex_arg_to_int(char const * arg, int len, int * result);
static bool binary_arg_to_int(char const * arg, int len, int * result);
static bool decimal_arg_to_int(char const * arg, int len, int * result);
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// static function definitions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int count_whitespaces(char const * arg, int len)
{
	for(int i = 0; i < len; i++)
	{
		if (!is_whitespace(arg[i])) return i;
	}
	return len;
}

static bool is_whitespace(char c)
{
	char const whitespaces[] = { '\n', '\t', ' ' };
	for (int i = 0; i < sizeof(whitespaces); i++)
	{
		if (c == whitespaces[i]) return true;
	}
	return false;
}

static bool has_suffix(char const * arg, int len, char const * suffix, int suffix_len)
{
	if (len < suffix_len) return false;
	if (memcmp(arg, suffix, suffix_len) == 0) return true;
	return false;
}

static bool hex_arg_to_int(char const * arg, int len, int * result)
{
	*result = 0;
	bool result_generated = false;
	for (int i = 0; i < len + 1; i += 2)
	{
		char const * pos = arg + i;
		int hex_val = 0;
		if (pos[0] >= '0' && pos[0] <= '9') hex_val = (pos[0] - '0') << 4;
		else if (pos[0] >= 'a' && pos[0] <= 'f') hex_val = (pos[0] - 'a' + 10) << 4;
		else if (pos[0] >= 'A' && pos[0] <= 'F') hex_val = (pos[0] - 'A' + 10) << 4;
		else if (is_whitespace(pos[0])) return true;
		if (pos[1] >= '0' && pos[1] <= '9') hex_val += (pos[1] - '0');
		else if (pos[1] >= 'a' && pos[1] <= 'f') hex_val += (pos[1] - 'a' + 10);
		else if (pos[1] >= 'A' && pos[1] <= 'F') hex_val += (pos[1] - 'A' + 10);
		else return false;
		*result = (*result << 8) + hex_val;
		result_generated = true;
	}
	return result_generated;
}

static bool binary_arg_to_int(char const * arg, int len, int * result)
{
	*result = 0;
	for (int i = 0; i < len; i++)
	{
		if (arg[i] == '1') *result = (*result << 1) + 1;
		else if (arg[i] == '0') *result = (*result << 1);
		else if (is_whitespace(arg[i])) return true;
		else return false;
	}
	return len > 0;
}

static bool decimal_arg_to_int(char const * arg, int len, int * result)
{
	*result = 0;
	int sign = 1;
	if (arg[0] == '-')
	{
		sign = -1;
		arg++;
	}
	for (int i = 0; i < len; i++)
	{
		if (arg[i] >= '0' && arg[i] <= '9') *result = (*result * 10) + arg[i] - '0';
		else if (is_whitespace(arg[i]))
		{
			*result *= sign;
			return true;
		}
		else return false;
	}
	*result *= sign;
	return len > 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int get_index(char const * input, int input_len, cmd_parser_command_t const * command_list, int command_count)
{
	for (int i = 0; i < command_count; i++)
	{
		int command_len = strlen(command_list[i].name);
		if (input_len < command_len) continue;
		if (memcmp(input, command_list[i].name, command_len) != 0) continue;
		if (input_len > (command_len + 1) && input[command_len + 1] > ' ')
		{
			if (input[command_len] != ' ') continue;
		}
		return i;
	}
	return CMD_PARSER_COMMAND_INVALID;
}

static print_line_values_t calculate_print_line_values(cmd_parser_command_t const * command_list, int command_count)
{
	print_line_values_t values = {0};
	for (int i = 0; i < command_count; i++)
	{
		int len = strlen(command_list[i].name);
		if (len > values.command_len) values.command_len = len;
		len = strlen(command_list[i].description);
		if (len > values.description_len) values.description_len = len;
	}
	values.complete_len = values.command_len + values.description_len + 4;
	return values;
}

int cmd_parser_parse(char const * input, int input_len, cmd_parser_command_t const * command_list, int command_count)
{
	int i = get_index(input, input_len, command_list, command_count);
	if (i == CMD_PARSER_COMMAND_INVALID) return CMD_PARSER_COMMAND_INVALID;
	char const * arg = NULL;
	int arg_len = 0;
	int command_len = strlen(command_list[i].name);
	if(input_len > (command_len + 1) && input[command_len] == ' ')
	{
		arg = input + command_len + 1;
		arg_len = input_len - command_len - 1;
	}
	command_list[i].command_fpt(&(command_list[i]), arg, arg_len);
	return i;
}

int cmd_parser_parse_and_set_context(char const * input, int input_len, cmd_parser_command_t * command_list, int command_count, void * p_context)
{
	int i = get_index(input, input_len, command_list, command_count);
	if (i == CMD_PARSER_COMMAND_INVALID) return CMD_PARSER_COMMAND_INVALID;
	char const * arg = NULL;
	int arg_len = 0;
	int command_len = strlen(command_list[i].name);
	if (input_len > (command_len + 1) && input[command_len] == ' ')
	{
		arg = input + command_len + 1;
		arg_len = input_len - command_len - 1;
	}
	command_list[i].p_context = p_context;
	command_list[i].command_fpt(&(command_list[i]), arg, arg_len);
	return i;
}

int cmd_parser_print_command(char * buffer, int buffer_size, cmd_parser_command_t const * command_list, int command_count, int index)
{
	if(index < 0) return 0;
	if(index >= command_count) return 0;
	print_line_values_t line_values = calculate_print_line_values(command_list, command_count);
	if(buffer_size <= line_values.complete_len) return buffer_size - line_values.complete_len - 1;
	int printed = snprintf(buffer, buffer_size, "%s", command_list[index].name);
	for(int i = printed; i < line_values.command_len; i++)
	{
		buffer[i] = ' ';
	}
	printed = line_values.command_len + snprintf(buffer + line_values.command_len, buffer_size - line_values.command_len, " - %s", command_list[index].description);
	return printed;
}

int cmd_parser_print_title(char * buffer, int buffer_size, cmd_parser_command_t const * command_list, int command_count, char const * title)
{
	print_line_values_t line_values = calculate_print_line_values(command_list, command_count);
	if(buffer_size <= line_values.complete_len) return buffer_size - line_values.complete_len - 1;
	int title_len = strlen(title);
	int dashes_count = (line_values.complete_len - title_len) / 2;
	if(dashes_count < 0) return -1;
	memset(buffer, '-', dashes_count);
	snprintf(buffer + dashes_count, buffer_size - dashes_count, title);
	memset(buffer + dashes_count + title_len, '-', line_values.complete_len - dashes_count - title_len);
	buffer[line_values.complete_len] = '\0';
	return line_values.complete_len;
}

bool cmd_parser_arg_to_int(char const * arg, int len, int * result)
{
	int whitespaces = count_whitespaces(arg, len);
	arg += whitespaces;
	len -= whitespaces;
	
	if (has_suffix(arg, len, "0x", 2)) return hex_arg_to_int(arg + 2, len, result);
	if (has_suffix(arg, len, "0b", 2)) return binary_arg_to_int(arg + 2, len, result);
	return decimal_arg_to_int(arg, len, result);
}