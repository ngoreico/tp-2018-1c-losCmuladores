/*
 ============================================================================
 Name        : instance.c
 Author      : losCmuladores
 Version     :
 Copyright   : Your copyright notice
 Description : Redistinto instance.
 ============================================================================
 */

#include "instance.h"
#include "instance-test.h"
#include "storage/entry-table.h"
#include "storage/file/file-system.h"
#include "response_codes.h"
#include <commons/log.h>
#include <stdbool.h>
#include "logging.h"

pthread_mutex_t atomic_operation = PTHREAD_MUTEX_INITIALIZER;

bool instance_running = true;

t_log * logger;

void _exit_with_error(char *error_msg, ...);
void _entry_table_log_data(t_entry_table *entry_table);
t_replacement_algorithm _replacement_algorithm_to_enum(char *replacement);

void init_logger() {
	logger = log_create("instance.log", "instance", true, LOG_LEVEL_DEBUG);
}

void exit_gracefully(int return_nr) {
	log_destroy(logger);
	entry_table_destroy(entries_table);
	instance_config_destroy(instance_config);
	close(coordinator_socket);
	exit(return_nr);
}


t_instance_config* instance_config_create() {
	t_instance_config *instance_config = malloc(sizeof(t_instance_config));
	instance_config->coordinator_ip = NULL;
	instance_config->instance_name = NULL;
	instance_config->mount_path = NULL;
	instance_config->replacement_algorithm = CIRCULAR;
	return instance_config;
}

void instance_config_destroy(t_instance_config *instance_config) {
	free(instance_config->coordinator_ip);
	free(instance_config->instance_name);
	free(instance_config->mount_path);
	free(instance_config);
}

t_instance_config* load_configuration(char* config_file_path){
	log_info(logger, "Loading instance configuration file...");

	t_config* config = config_create(config_file_path);

	t_instance_config *instance_config = instance_config_create();
	instance_config->coordinator_port = config_get_int_value(config, "COORDINATOR_PORT");
	instance_config->coordinator_ip = string_duplicate(config_get_string_value(config, "COORDINATOR_IP"));
	instance_config->instance_name = string_duplicate(config_get_string_value(config, "NAME"));
	instance_config->mount_path = string_duplicate(config_get_string_value(config, "MOUNT_PATH"));
	instance_config->dump_interval = config_get_int_value(config, "DUMP_INTERVAL");

	char *replacement_algorithm_str = config_get_string_value(config, "REPLACEMENT_ALGORITHM");
	instance_config->replacement_algorithm = _replacement_algorithm_to_enum(replacement_algorithm_str);

	config_destroy(config);

	log_info(logger, "Instance configuration loaded OK");
	return instance_config;
}

t_instance_configuration *receive_instance_configuration(int socket){
	t_instance_configuration *instance_configuration = (t_instance_configuration*) malloc(sizeof(t_instance_configuration));
	log_info(logger, "Receiving instance configuration from coordinator.");

	int status = recv(socket, instance_configuration, sizeof(t_instance_configuration), MSG_WAITALL);
	if(status <= 0){
		free(instance_configuration);
		_exit_with_error("Could not receive instance configuration");
	}

	log_info(logger, "Configuration successfully received !");
	return instance_configuration;
}

t_list *receive_keys_to_load(int socket) {
	log_info(logger, "Receiving keys to load from coordinator.");

	t_list *keys = NULL;
	int key_count;
	int key_count_status = recv(socket, &key_count, sizeof(key_count), MSG_WAITALL);

	if (key_count_status > 0) {
		keys = list_create();
		for (int i = 0; i < key_count; ++i) {
			char *key;
			int key_result = recv_string(socket, &key);
			if (key_result > 0) {
				list_add(keys, (void *)key);
			} else {
				log_error(logger, "Error receiving key to load from coordinator");
				list_destroy(keys);
				keys = NULL;
				break;
			}
		}
	} else {
		log_error(logger, "Error receiving keys count from coordinator");
	}
	log_info(logger, "Received %d keys to load from coordinator.", list_size(keys));
	return keys;
}

void check_if_connection_was_ok(int server_socket){
	 if(server_socket == -1){
		  log_error(logger, "Could not connect with coordinator.");
		  exit_gracefully(1);
	  }

	  log_info(logger, "Connected !");
}

int process_sentence(t_sentence* sentence) {
	log_info(logger, "Processing statement...");
	switch(sentence->operation_id) {
	case GET_SENTENCE:;
		char *value = entry_table_get(entries_table, sentence->key);
		if (NULL == value) {
			log_info(logger, "Value not present for key %s", sentence->key);
		} else {
			log_info(logger, "Value %s get for key %s", value, sentence->key);
		}
		return 0;
	case SET_SENTENCE:
		return entry_table_put(entries_table, sentence->key, sentence->value);
	case STORE_SENTENCE:
		return entry_table_store(entries_table, instance_config->mount_path, sentence->key);
	default:
		return -1;
	}
}


//TODO hacer deserializador.
t_sentence* wait_for_statement(int socket_fd) {
	t_sentence *sentence = sentence_create();

	if (recv_sentence_operation(socket_fd, &sentence->operation_id) > 0) {
		if (recv_string(socket_fd, &sentence->key) > 0) {
			if (recv_string(socket_fd, &sentence->value) > 0) {
				char *sentence_str = sentence_to_string(sentence);
				log_info(logger, "Sentence successfully received: %s", sentence_str);
				free(sentence_str);
				return sentence;
			} else {
				log_error(logger, "Could not receive sentence value.");
			}
		} else {
			log_error(logger, "Could not receive sentence key.");
		}
	} else {
		log_error(logger, "Could not receive sentence operation id.");
	}

	sentence_destroy(sentence);
	return NULL;
}

void signal_handler(int sig){
    if (sig == SIGINT) {
    	log_info(logger, "Caught signal for Ctrl+C");
    	close(coordinator_socket);
    	instance_running = false;
    }
}

void send_result(int coordinator_socket, int result){
	int send_result = send(coordinator_socket, &result, sizeof(result), 0);

	if(send_result <= 0){
		_exit_with_error("Could not send result %d to coordinator.", result);
	}
}

void send_sentence_result(int coordinator_socket, int result, int used_entries){
	int size = sizeof(result) + sizeof(used_entries);
	int *data = (int *)malloc(size);
	memcpy(data, &result, sizeof(result));
	memcpy(data + 1, &used_entries, sizeof(used_entries));
	int send_result = send(coordinator_socket, data, size, 0);
	free(data);

	if(send_result <= 0){
		_exit_with_error("Could not send result %d to coordinator.", result);
	}
}

void send_instance_name(int coordinator_socket, char *instance_name) {
	log_info(logger, "Sending Instance name to coordinator...");
	int instance_name_length = strlen(instance_name) + 1;

	int message_size = sizeof(int) + instance_name_length;

	void* buffer = malloc(message_size);
	void* offset = buffer;

	concat_string(&offset, instance_name, instance_name_length);

	send(coordinator_socket, buffer, message_size, 0);
	free(buffer);

	int confirmation_response;

	int name_response = recv(coordinator_socket, &confirmation_response, sizeof(int), 0);
	if(name_response <= 0){
		_exit_with_error("Could not receive instance name confirmation from coordinator.");
	}

	if(confirmation_response != 1) {
		_exit_with_error("Instance name error. Maybe other instance with same name is already running.");
	}
	log_info(logger, "Instance name sent OK.");
}

void create_mount_path(char* mounting_path) {
	if (create_folder(mounting_path) == -1) {
		_exit_with_error("Error when trying to create mounting path: %s", mounting_path);
	}
}

void wait_for_key_value_requests(int socket) {
	char* key;
	int key_received = recv_string(socket, &key);
	if (key_received == 0) {
		log_error(logger, "[KeyInfoRequest] Coordinator has disconnected!");
		instance_running = false;
	} else if (key_received < 0) {
		log_error(logger, "[KeyInfoRequest] Could not receive key");
	} else {
		char* value = entry_table_get(entries_table, key);
		if (value != NULL) {
			int value_size = strlen(value) + 1;
			int buffer_size = sizeof(execution_result) + sizeof(int) + value_size;
			void* buffer = malloc(buffer_size);
			void* offset = buffer;
			execution_result result = KEY_VALUE_FOUND;
			concat_value(&offset, &result, sizeof(execution_result));
			concat_string(&offset, value, value_size);

			if (send(socket, buffer, buffer_size, 0) < 0) {
				log_error(logger, "[KeyInfoRequest] Could not send value");
			}
			free(buffer);
		} else {
			execution_result result = KEY_VALUE_NOT_FOUND;
			if (send(socket, &result, sizeof(execution_result), 0) < 0) {
				log_error(logger,
						"[KeyInfoRequest] Could not send key value not found message");
			}
		}
	}
}

void assert_not_blank(char* msg, char* arg) {
	if (arg == NULL || string_is_empty(arg)) {
		log_error(logger, msg);
		log_destroy(logger);
		exit(1);
	}
}

int instance_run(int argc, char* argv[]) {
	log_info(logger, "Initializing instance...");
    signal(SIGINT,signal_handler);

    assert_not_blank("Archivo de configuracion requerido!", argv[1]);
	instance_config = load_configuration(argv[1]);
	create_mount_path(instance_config->mount_path);

	coordinator_socket = connect_to_coordinator(instance_config->coordinator_ip, instance_config->coordinator_port);

	send_instance_name(coordinator_socket, instance_config->instance_name);

	t_instance_configuration *configuration = receive_instance_configuration(coordinator_socket);
	entries_table = entry_table_create(configuration->entries_quantity, configuration->entries_size, instance_config->replacement_algorithm);
	free(configuration);
	t_list *keys_to_load = receive_keys_to_load(coordinator_socket);
	entry_table_load_list(entries_table, instance_config->mount_path, keys_to_load);
	list_destroy_and_destroy_elements(keys_to_load, free);

	log_info(logger, "Initializing instance... OK");

	pthread_t dump_thread = start_dump_interval();

	while(instance_running) {
		log_info(logger, "Waiting for sentence from coordinator...");

		message_type request = recv_message(coordinator_socket);
		if (request == 0) {
			log_error(logger, "Coordinator has disconnected!");
			instance_running = false;
		} else if (request == PROCESS_SENTENCE) {
			t_sentence* sentence = wait_for_statement(coordinator_socket);
			if (NULL != sentence) {
				pthread_mutex_lock(&atomic_operation);
				int sentence_result = process_sentence(sentence);
				sentence_destroy(sentence);

				_entry_table_log_data(entries_table);

				int coordinator_result = sentence_result;
				if (sentence_result >= 0) {
					coordinator_result = OK;
				} else if (sentence_result == -2) {
					log_info(logger, "Sending NEED_COMPACTION message to coordinator");
					coordinator_result = NEED_COMPACTION;
				}
				int used_entries = availability_get_taken_entries_count(entries_table->availability);
				log_info(logger, "Used entries: %d", used_entries);
				pthread_mutex_unlock(&atomic_operation);
				send_sentence_result(coordinator_socket, coordinator_result, used_entries);
			} else {
				_exit_with_error("Error receiving sentence from coordinator. Maybe was disconnected");
			}
		} else if (request == GET_KEY_VALUE) {
			wait_for_key_value_requests(coordinator_socket);
		} else if (request == START_COMPACTION) {
			pthread_mutex_lock(&atomic_operation);
			entry_table_compact(entries_table);
			pthread_mutex_unlock(&atomic_operation);
			send_result(coordinator_socket, OK);
		} else if (request == HEALTH_CHECK) {
			send_result(coordinator_socket, OK);
		}
	}

	log_info(logger, "Waiting dump thread to finish");
	pthread_join(dump_thread, NULL);
	exit_gracefully(1);
	return 0;
}

int main(int argc, char* argv[]) {
	init_logger();
	if (argc > 2 && string_equals_ignore_case(argv[2], "--test")) {
		return instance_run_test();
	} else {
		return instance_run(argc, argv);
	}
}

int connect_to_coordinator(char *coordinator_ip, int coordinator_port) {
	log_info(logger, "Connecting with coordinator.");

	int coordinator_socket = connect_to(coordinator_ip, coordinator_port);

	if (coordinator_socket < 0) {
		_exit_with_error("No se pudo conectar al coordinador");
	} else if (send_module_connected(coordinator_socket, INSTANCE) < 0) {
		_exit_with_error("No se pudo enviar al confirmacion al coordinador");
	} else {
		message_type message_type;
		int message_type_result = recv(coordinator_socket, &message_type,
				sizeof(message_type), MSG_WAITALL);

		if (message_type_result < 0 || message_type != CONNECTION_SUCCESS) {
			_exit_with_error("Error al recibir confirmacion del coordinador");
		} else {
			log_info(logger, "Connexion con el coordinador establecida");
		}
	}
	return coordinator_socket;
}

void dump_interval() {
	while(instance_running) {
		usleep(instance_config->dump_interval * 1000000);
		log_info(logger, "Starting dump interval");

		pthread_mutex_lock(&atomic_operation);
		int result = entry_table_store_all(entries_table, instance_config->mount_path);
		pthread_mutex_unlock(&atomic_operation);
		if (result <= 0) {
			_exit_with_error("Error executing dump interval");
		}
		log_info(logger, "Finish dump interval");
	}
}

pthread_t start_dump_interval() {
	pthread_t dump_thread;
	if (pthread_create(&dump_thread, NULL, (void*) dump_interval, NULL) < 0) {
		_exit_with_error("Error starting dump interval thread");
	}
	return dump_thread;
}

t_replacement_algorithm _replacement_algorithm_to_enum(char *replacement) {
	if (strcmp(replacement, "CIRC") == 0) {
		return CIRCULAR;
	} else if (strcmp(replacement, "LRU") == 0) {
		return LRU;
	} else if (strcmp(replacement, "BSU") == 0) {
		return BSU;
	} else {
		_exit_with_error("El algoritmo de reemplazo %s es invalido", replacement);
		return -1;
	}
}

void _exit_with_error(char *error_msg, ...) {
	va_list arguments;
	va_start(arguments, error_msg);
	char *formatted_message = string_from_vformat(error_msg, arguments);
	va_end(arguments);
	log_error(logger, formatted_message);
	free(formatted_message);
	pthread_mutex_destroy(&atomic_operation);
	exit_gracefully(1);
}

void _entry_table_log_data(t_entry_table *entry_table) {
	int size = sizeof(char) * entry_table->max_entries * entry_table->entry_size;
	char *data = malloc(size + 1);

	memcpy(data, entry_table->data, size);
	*(data + size) = '\0';

	log_debug(logger, "Data table: %s", data);

	free(data);
}
