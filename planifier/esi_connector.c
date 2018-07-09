/*
 * esi_connection_handler.c
 *
 *  Created on: 24 jun. 2018
 *      Author: utnso
 */

#include "esi_connector.h"

long new_esi(int socket, long esi_size);
module_type recv_module(int socket);
long recv_esi_size(int socket);
int send_id_to_esi(int socket, long esi_id);

void esi_connection_handler(int socket) {
	if (recv_message(socket) != MODULE_CONNECTED) {
		log_info(logger, "Connection was received but the message type does not imply connection. Ignoring");
		close(socket);
		return;
	}

	if (recv_module(socket) <= 0) {
		return;
	}

	log_info(logger, "ESI connected! (from %s)", get_client_address(socket));
	long esi_size = recv_esi_size(socket);
	if (esi_size <= 0) {
		return;
	}

	long new_esi_id = new_esi(socket, esi_size);
	log_info(logger, "New ESI created with id %ld was added to ready queue", new_esi_id);

	if (send_id_to_esi(socket, new_esi_id) < 0) {
		log_error(logger, "Error sending the id to the new ESI. Client-Address %s", get_client_address(socket));
		return;
	}
	notify_dispatcher();
}

long new_esi(int socket, long esi_size){
	esi* new_esi = malloc(sizeof(esi));
	new_esi -> id = increment_id();
	new_esi -> estado = LISTO;
	new_esi -> tiempo_de_entrada = get_current_time();
	new_esi -> socket_id = socket;
	new_esi -> esi_thread = pthread_self();
	pthread_mutex_unlock(&cpu_time_mtx);
	new_esi -> cantidad_de_instrucciones = esi_size;
	new_esi -> instruction_pointer = 0;
	pthread_mutex_unlock(&cpu_time_mtx);
	add_esi(new_esi);
	return new_esi -> id;
}

module_type recv_module(int socket) {
	module_type module;
	int module_received = recv(socket, &module, sizeof(module_type), MSG_WAITALL);
	if (module_received <= 0) {
		log_error(logger, "Error trying to receive module type. Closing connection");
		close(socket);
		return module_received;
	}
	if (module != ISE) {
		log_info(logger, "Ignoring connected client because it was not an ESI");
		close(socket);
		return -1;
	}
	return module;
}

long recv_esi_size(int socket) {
	long esi_size;
	int result_esi_size = recv_long(socket, &esi_size);
	if (result_esi_size <= 0) {
		log_error(logger, "Error trying to receive ESI size. Closing connection");
		close(socket);
		return result_esi_size;
	}
	return esi_size;
}

int send_id_to_esi(int socket, long esi_id) {
	int message_size = sizeof(message_type) + sizeof(long);
	void* buffer = malloc(message_size);
	void* offset = buffer;
	concat_value(&offset, &CONNECTION_SUCCESS, sizeof(message_type));
	concat_value(&offset, &esi_id, sizeof(long));
	int result_send_new_esi_id = send(socket, buffer, message_size, 0);
	free(buffer);
	if (result_send_new_esi_id < 0) {
		close(socket);
	}
	return result_send_new_esi_id;
}

bool wait_execution_result(long esi_id) {
	esi* esi_to_get_result_from = dictionary_get(esi_map, id_to_string(esi_id));
	int socket = esi_to_get_result_from->socket_id;

	if (recv_message(socket) != EXECUTION_RESULT) {
		return false;
	}

	execution_result esi_execution_result;
	int esi_execution_result_status = recv(socket, &esi_execution_result, sizeof(execution_result), MSG_WAITALL);
	if (esi_execution_result_status <= 0) {
		log_error(logger, "Error trying to receive the execution result");
		//TODO QUE HAGO SI NO PUDE RECIBIR BIEN EL RESULTADO?
		return false;
	}
	return true;
}

int send_message_to_esi(long esi_id, message_type message) {
	pthread_mutex_lock(&esi_map_mtx);
	esi* esi_to_notify = dictionary_get(esi_map, id_to_string(esi_id));
	pthread_mutex_unlock(&esi_map_mtx);
	int socket_id = esi_to_notify->socket_id;
	return send(socket_id, &message, sizeof(message_type), 0);
}

bool send_esi_to_run(long esi_id) {
	if (send_message_to_esi(esi_id, ISE_EXECUTE) < 0){
		log_error(logger, "Could not send ise %ld to run", esi_id);
		return false;
		//todo que pasa si no le puedo mandar un mensaje?
	}
	return true;
}