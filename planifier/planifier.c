/*
 ============================================================================
 Name        : planifier.c
 Author      : losCmuladores
 Version     :
 Copyright   : Your copyright notice
 Description : Redistinto planifier	.
 ============================================================================
 */

#include "planifier.h"

long esi_id_generate(){
	pthread_mutex_trylock(&id_mtx);
	id++;
	long new_id = id;
	pthread_mutex_unlock(&id_mtx);
	return new_id;
}

long cpu_time_incrementate(){
	pthread_mutex_trylock(&cpu_time_mtx);
	int new_cpu_time = cpu_time ++;
	pthread_mutex_unlock(&cpu_time_mtx);
	return new_cpu_time;
}

long new_esi(int socket, long esi_size){
	esi* new_esi = malloc(sizeof(esi));
	new_esi -> id = esi_id_generate();
	new_esi -> estado = NUEVO;
	new_esi -> tiempo_de_entrada = cpu_time;
	new_esi -> socket_id = socket;
	new_esi -> esi_thread = pthread_self();
	pthread_mutex_unlock(&cpu_time_mtx);
	new_esi -> cantidad_de_instrucciones = esi_size;
	new_esi -> instrucction_pointer = 0;
	pthread_mutex_unlock(&cpu_time_mtx);
	add_esi(new_esi);
	return (new_esi -> id);
}

//che_ejecute_esto(tiene input output){ //esi diciendo ejecute algo
//bool hubo_replanificacion_con_cambio_de_esi
///*este bool lo tiene que tener por referencia los algoritmos
// * y poder modificarlo cada vez que hay replanificacion usando semaforos*/
//che_ejecute_esto(int esi_id){
/*me da algo, tendria que ser el id del esi, y me podria decir que es la ultima instruccion*/
//	if(hubo_replanificacion_con_cambio_de_esi){
//
//	}else{
//
//}
//}
//

int send_message_to_esi(long esi_id, message_type message){
	esi* esi_to_notify = dictionary_get(esi_map, string_key(esi_id));
	int socket_id = esi_to_notify->socket_id;
	return send(socket_id, &message, sizeof(message_type), 0);
}

bool send_esi_to_run(){
	long esi_id = esi_se_va_a_ejecutar();
	if (send_message_to_esi(esi_id, ISE_EXECUTE) < 0){
		log_error(logger, "Could not send ise %ld to run", esi_id);
		return false;
		//todo que pasa si no le puedo mandar un mensaje?
	}
	return true;
}

void send_esi_to_stop(long esi_id){
	if (send_message_to_esi(esi_id, ISE_STOP) < 0){
		log_error(logger, "Could not send ise %ld to run", esi_id);
		//todo que pasa si no le puedo mandar un mensaje?
	}
}

//che_esta_tomado_el_recurso(input_outpu)

bool bloquear_recurso(char* recurso, long esi_id){
	pthread_mutex_lock(&blocked_by_resource_map_mtx);
	if(!dictionary_has_key(recurso_tomado_por_esi,recurso)){
		dictionary_put(recurso_tomado_por_esi, recurso, queue_create());
		pthread_mutex_unlock(&blocked_by_resource_map_mtx);
		return true;
	}
	t_queue* cola_de_esis = dictionary_get(recurso_tomado_por_esi,recurso);
	esi *running_esi = malloc(sizeof(esi));
//	running_esi = get_esi_running();
//	queue_push(cola_de_esis,(running_esi->id));
//	stop_and_block_esi(running_esi -> id);
	pthread_mutex_unlock(&blocked_by_resource_map_mtx);
	return false;
}

bool deshabilitar_recurso(char*/*no se que es esto*/ recurso, long esi_id_desabilitado){
	// TODO [Lu] revisar &blocked_esi_type
	pthread_mutex_lock(&blocked_by_resource_map_mtx);
	long blocked_esi_type = ESI_BLOQUEADO;
	if(!dictionary_has_key(recurso_tomado_por_esi, recurso)){
		dictionary_put(recurso_tomado_por_esi, recurso, &blocked_esi_type);
		dictionary_put(esis_bloqueados_por_recurso, recurso, queue_create());
		pthread_mutex_unlock(&blocked_by_resource_map_mtx);
		return true;
	}
	long* esi_id = dictionary_get(recurso_tomado_por_esi,recurso);
	if(!(*esi_id == esi_id_desabilitado)){
		t_queue* cola_de_esis = dictionary_get(esis_bloqueados_por_recurso,recurso);
		queue_push(cola_de_esis, esi_id);
//		stop_and_block_esi(esi_id);
	}
	finish_esi(*esi_id);
	dictionary_put(recurso_tomado_por_esi, recurso, &blocked_esi_type);
	pthread_mutex_unlock(&blocked_by_resource_map_mtx);
}


//esi* get_esi_running(){
//	return list_get(RUNNING_ESI_LIST, 0);
//}

//-------------------
void load_configuration(char *config_file_path) {
	log_info(logger, "Loading planifier configuration file...");
	t_config* config = config_create(config_file_path);

	algorithm = config_get_int_value(config, "ALGORITHM");

	server_port = config_get_int_value(config, "SERVER_PORT");
	server_max_connections = config_get_int_value(config,
			"MAX_ACCEPTED_CONNECTIONS");

	coordinator_port = config_get_int_value(config, "COORDINATOR_PORT");
	coordinator_ip = string_duplicate(config_get_string_value(config, "COORDINATOR_IP"));

	config_destroy(config);

	log_info(logger, "Planifier configuration file loaded");
}

void connect_to_coordinator() {
	coordinator_socket = connect_to(coordinator_ip, coordinator_port);

	if (coordinator_socket < 0) {
		exit_with_error(coordinator_socket, "No se pudo conectar al coordinador");
	} else if (send_module_connected(coordinator_socket, PLANIFIER) < 0) {
		exit_with_error(coordinator_socket, "No se pudo enviar al confirmacion al coordinador");
	} else {
		message_type message_type;
		int message_type_result = recv(coordinator_socket, &message_type,
				sizeof(message_type), MSG_WAITALL);

		if (message_type_result < 0 || message_type != CONNECTION_SUCCESS) {
			exit_with_error(coordinator_socket, "Error al recibir confirmacion del coordinador");
		} else {
			log_info(logger, "Connexion con el coordinador establecida");
		}

		int operation;
		while(recv_sentence_operation(coordinator_socket, &operation) > 0){//todo que condicion pongo aca?
			char* resource;
			get_resource(&resource);
			long esi_id;
			get_esi_id(&esi_id);
			execution_result result;
			switch(operation){
				case GET_SENTENCE:
					try_to_block_resource(resource, esi_id);
					break;
				case SET_SENTENCE:
					if (el_esi_puede_tomar_el_recurso(esi_id, resource)){
						result = OK;
					}else{
						result = KEY_LOCK_NOT_ACQUIRED;
					}
					send(coordinator_socket, &result, sizeof(execution_result), 0);
					break;
				case STORE_SENTENCE:
					free_resource(resource);
					break;
				case KEY_UNREACHABLE:
					result = OK;
					send(coordinator_socket, &result, sizeof(execution_result), 0);
					break;
				default:
					log_info(logger, "Connection was received but the operation its not supported. Ignoring");
					break;
			}
		}
	}
}

int get_resource(char** resource){
	if(recv_string(coordinator_socket, resource) <= 0){
		log_info(logger, "Could not get the resource");
		//TODO que hago si no lo pude recibir?
		return 0;
	}
	return 1;
}

int get_esi_id(long* esi_id){
	if(recv_long(coordinator_socket, esi_id) < 0){
		log_info(logger, "Could not get the esi id");
		//TODO que hago si no lo pude recibir?
		return 0;
	}
	return 1;
}

void send_execution_result_to_coordinator(execution_result result){
	if(send(coordinator_socket, &result, sizeof(execution_result), 0) <0){
		log_info(logger, "Could not send response to coordinator");
		//TODO que hago si no lo pude recibir?
	}
}

void free_resource(char* resource){
	pthread_mutex_lock(&blocked_by_resource_map_mtx);
	//TODO ver que onda desbloqueo todo o una sola. UPDATE: habiamos quedado en desbloquear solo una, no?
	dictionary_remove(recurso_tomado_por_esi, resource);
	t_queue* esi_queue = dictionary_get(esis_bloqueados_por_recurso, resource);
	long* esi_id = queue_pop(esi_queue);
	while (!is_valid_esi(*esi_id)) {
		esi_id = queue_pop(esi_queue);
	}
	pthread_mutex_unlock(&blocked_by_resource_map_mtx);
	add_esi_bloqueada(*esi_id);
	//TODO OJO AL PIOJO el free de datos como el id que guardamos de la esi bloqueada;

	send_execution_result_to_coordinator(OK);
}

//bool el_esi_puede_tomar_el_recurso(long* esi_id, char **resource){
bool el_esi_puede_tomar_el_recurso(long esi_id, char* resource){
//	pthread_mutex_lock(&map_boqueados);
//	bool result = dictionary_has_key(recurso_tomado_por_esi,resource)
//					&& *(dictionary_get(recurso_tomado_por_esi, resource)) == esi_id;
//	pthread_mutex_unlock(&map_boqueados);
//	return result;
	return false;
}

void try_to_block_resource(char* resource, long esi_id){
	log_debug(logger, "Trying to block resource %s for ESI%ld", resource, esi_id);
	if (bloquear_recurso(resource, esi_id)){
		send_execution_result_to_coordinator(OK);
	}else{
		send_execution_result_to_coordinator(KEY_BLOCKED);
	}
}

void could_use_resource(char* resource, long esi_id) {
	if (el_esi_puede_tomar_el_recurso(esi_id, resource)){
		send_execution_result_to_coordinator(OK);
	}else{
		send_execution_result_to_coordinator(KEY_LOCK_NOT_ACQUIRED);
	}
}

void esi_connection_handler(int socket){
	message_type message_type_result = recv_message(socket);
	if(message_type_result != MODULE_CONNECTED){
		log_info(logger, "Connection was received but the message type does not imply connection. Ignoring");
		close(socket);
		return;
	}

	module_type module;
	int result_module_type = recv(socket, &module, sizeof(module_type), MSG_WAITALL);
	if (result_module_type <= 0) {
		log_error(logger, "Error trying to receive module type. Closing connection");
		close(socket);
		return;
	}
	if (module == ISE) {
		log_info(logger, "ESI connected! (from %s)", get_client_address(socket));

		long esi_size;
		int result_esi_size = recv_long(socket, &esi_size);
		if (result_esi_size <= 0) {
			log_error(logger, "Error trying to receive message. Closing connection");
			close(socket);
			return;
		}
		long new_esi_id = new_esi(socket, esi_size);
		log_info(logger, "New ESI created with id %ld was added to ready queue", new_esi_id);
		int message_size = sizeof(message_type) + sizeof(long);
		void* buffer = malloc(message_size);
		void* offset = buffer;
		concat_value(&offset, &CONNECTION_SUCCESS, sizeof(message_type));
		concat_value(&offset, &new_esi_id, sizeof(long));
		int result_send_new_esi_id = send(socket, buffer, message_size, 0);
		free(buffer);
		if (result_send_new_esi_id < 0) {
			log_error(logger, "Error sending the id to the new esi. Client-Address %s",	get_client_address(socket));
			close(socket);
			return;
		}

		if (es_caso_base(new_esi_id)) {
			if (send_esi_to_run()){
				esi_execution_result_handler(socket);
			} else {
				volver_caso_base();
			}
		}
	} else {
		log_info(logger, "Ignoring connected client because it was not an ESI");
	}

}

void esi_execution_result_handler(int socket){
	execution_result esi_execution_result;

	int esi_execution_result_status = recv(socket, &esi_execution_result, sizeof(execution_result), MSG_WAITALL);
	if (esi_execution_result_status <= 0) {
		log_error(logger, "Error trying to receive the execution result");
		//TODO QUE HAGO SI NO PUDE RECIBIR BIEN EL RESULTADO?
		return;
	}
	cpu_time_incrementate();
	send_esi_to_run();
	borado_de_finish();
}

int main(int argc, char* argv[]) {
	esi_map = dictionary_create();
	esis_bloqueados_por_recurso = dictionary_create();
	int i = 1;
	set_orchestrator(i);
	init_logger();
	load_configuration(argv[1]);
	int server_started = start_server(server_port, server_max_connections, (void *) esi_connection_handler, true, logger);
	if (server_started < 0) {
		log_error(logger, "Server not started");
	}

	pthread_t console_thread = start_console();
	connect_to_coordinator();
	pthread_join(console_thread, NULL);
	return EXIT_SUCCESS;
}
