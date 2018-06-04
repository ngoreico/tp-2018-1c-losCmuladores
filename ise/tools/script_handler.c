#include "script_handler.h"

void load_script(char * file_name) {
	FILE * script_file = fopen(file_name, "r");

	if (script_file == NULL) {
		log_error(logger, "File %s not found", file_name);
		exit_with_error();
	}

	script = malloc(sizeof(t_ise_script));
	script->lines = queue_create();
	while (!(feof(script_file))) {
		size_t line_size = 0;
		char * line;
		bool valid_line = getline(&line, &line_size, script_file) != -1;
		if (valid_line) {
			queue_push(script->lines, line);
		}
	}
	fclose(script_file);
}

t_ise_sentence next_sentence() {
	char* line = queue_pop(script->lines);
	t_ise_sentence sentence;
	if (line != NULL) {
		string_trim(&line);
		sentence.operation = parse(line);
		sentence.empty = false;
	} else {
		sentence.empty = true;
	}
	return sentence;
}

void destroy_script(t_ise_script * script) {
	if (script != NULL) {
		if (script->lines != NULL) {
			queue_destroy(script->lines);
		}
		free(script);
	}
}

void print_script(t_ise_script * script) {
	for (int i = 0; i < queue_size(script->lines); i++) {
		char* instruction = list_get(script->lines->elements, i);
		printf("%s", instruction);
	}
}

t_sentence* map_to_sentence(t_esi_operacion operation) {
	t_sentence* sentence = malloc(sizeof(t_sentence));
	switch(operation.keyword) {
	case GET:
		sentence->operation_id = GET_SENTENCE;
		sentence->key = operation.argumentos.GET.clave;
		sentence->value = "";
		break;
	case SET:
		sentence->operation_id = SET_SENTENCE;
		sentence->key = operation.argumentos.SET.clave;
		sentence->value = operation.argumentos.SET.valor;
		break;
	case STORE:
		sentence->operation_id = STORE_SENTENCE;
		sentence->key = operation.argumentos.STORE.clave;
		sentence->value = "";
		break;
	default:
		log_error(logger,"Unexpected error: sentence keyword not recognized");
		exit_with_error();
	}
	return sentence;
}

long get_script_size() {
	return script == NULL ? 0 : (script->lines == NULL ? 0 : queue_size(script->lines));
}

