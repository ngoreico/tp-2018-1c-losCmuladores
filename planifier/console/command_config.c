/*
 * config.c
 *
 *  Created on: 8 jun. 2018
 *      Author: utnso
 */

#include "command_config.h"

t_dictionary* commands;

void add_command(char* command_str, command_code code, int args) {
	command_config* command = malloc(sizeof(command_config));
	command->code = code;
	command->args_count = args;
	dictionary_put(commands, command_str, command);
}

void load_commands() {
	commands = dictionary_create();
	add_command("exit", EXIT, 0);
	add_command("pause", PAUSE, 0);
	add_command("block", BLOCK, 2);
	add_command("unblock", UNBLOCK, 1);
	add_command("list", LIST, 1);
}

command_config* get_command_config(char* command_str) {
	if (command_str == NULL || string_is_empty(command_str)) {
		return NULL;
	}
	return dictionary_get(commands, command_str);
}

command_code get_command_code(char* command_str) {
	command_config* command = get_command_config(command_str);
	if (command != NULL) {
		return command->code;
	}
	return UNKNOWN;
}

void destroy_command_config() {
	dictionary_destroy(commands);
}

