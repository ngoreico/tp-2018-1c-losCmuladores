/*
 * block_command.c
 *
 *  Created on: 9 jun. 2018
 */

#include "block_cmd.h"

typedef struct {
	char* esi_id;
	char* resource;
} command_info;

void try_block(command_info* command_info) {
	make_wait_for_resource(id_as_long(command_info->esi_id), command_info->resource);
	block_esi(id_as_long(command_info->esi_id));
}

void async_block(command_info* command_info) {
	pthread_t thread;
	pthread_create(&thread, NULL, (void*) try_block, (void*) command_info);
	pthread_join(thread, NULL);
	free(command_info);
}

command_result block_cmd(command command) {
	command_result result;

	char* resource = list_get(command.args, 0);
	char* esi_id = list_get(command.args, 1);

	if (esi_map == NULL) {
		result.code = COMMAND_ERROR;
		result.content = string_from_format("ESI with id '%s' does not exist", esi_id);
		return result;
	}

	esi* esi = dictionary_get(esi_map, esi_id);
	if (esi == NULL) {
		result.code = COMMAND_ERROR;
		result.content = string_from_format("ESI with id '%s' does not exist", esi_id);
		return result;
	}

	pthread_t thread;
	command_info* cmd_info = malloc(sizeof(command_info));
	cmd_info->esi_id = esi_id;
	cmd_info->resource = resource;
	pthread_create(&thread, NULL, (void*) async_block, (void*) cmd_info);

	result.code = COMMAND_OK;
	result.content = string_from_format("Ok! ESI %s will be blocked in the next possible opportunity", esi_id);
	return result;
}
