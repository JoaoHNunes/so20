#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/time.h>
#include "fs/operations.h"

#define MAX_COMMANDS 150000
#define MAX_INPUT_SIZE 100


char inputCommands[MAX_COMMANDS][MAX_INPUT_SIZE];
int numberCommands = 0;
int headQueue = 0;


char syncStrategy;

pthread_mutex_t commandLock;
pthread_mutex_t globalLock;
pthread_rwlock_t rwlock;

int insertCommand(char* data) {
    if(numberCommands != MAX_COMMANDS) {
        strcpy(inputCommands[numberCommands++], data);
        return 1;
    }
    return 0;
}

char* removeCommand() {
    if(numberCommands > 0){
        numberCommands--;
        return inputCommands[headQueue++];  
    }
    return NULL;
}

void errorParse(){
    fprintf(stderr, "Error: command invalid\n");
    exit(EXIT_FAILURE);
}

//funcao que verifica que tipo de lock vai ser bloqueado e bloqueia
void lock(char c){
    switch (syncStrategy){
        case 'm':
            if (pthread_mutex_lock(&globalLock) != 0){
                perror("Error");
                exit(EXIT_FAILURE);
            }
            break;

        case 'r':
            switch (c){
                case 'w':
                    if (pthread_rwlock_wrlock(&rwlock) != 0){
                        perror("Error");
                        exit(EXIT_FAILURE);
                    }
                    break;

                case 'r':
                    if (pthread_rwlock_rdlock(&rwlock) != 0){
                        perror("Error");
                        exit(EXIT_FAILURE);
                    } 
                    break;

                default:
                    return;
            }
            break;

        case 'n':
            return;

        default:
            fprintf(stderr, "Error: synchstrategy invalid\n");
            exit(EXIT_FAILURE);
    }
}

//funcao que verifica que tipo de lock vai ser desbloqueado e desbloqueia
void unlock(){
    switch (syncStrategy){
        case 'm':
            if (pthread_mutex_unlock(&globalLock) != 0){
                perror("Error");
                exit(EXIT_FAILURE);
            }
            break;

        case 'r':
            if (pthread_rwlock_unlock(&rwlock) != 0){
                perror("Error");
                exit(EXIT_FAILURE);
            }
            break;

        case 'n':
            return;

        default:
            fprintf(stderr, "Error: synchstrategy invalid\n");
            exit(EXIT_FAILURE);
    }
}

//funcao que destroi todos os locks inicializados
void destroyLocks(){
    switch (syncStrategy){
        case 'm':
            if (pthread_mutex_destroy(&globalLock) != 0){
                perror("Error");
                exit(EXIT_FAILURE);
            }
            break;

        case 'r':
            if (pthread_rwlock_destroy(&rwlock) != 0){
                perror("Error");
                exit(EXIT_FAILURE);
            }
            break;

        case 'n':
            return;

        default:
            fprintf(stderr, "Error: synchstrategy invalid\n");
            exit(EXIT_FAILURE);
    }
    if (syncStrategy != 'n'){
        if (pthread_mutex_destroy(&commandLock) != 0){
            perror("Error");
            exit(EXIT_FAILURE);
        }
    }
}


void processInput(char* fileName){
    char line[MAX_INPUT_SIZE];

    FILE *fp;
	fp = fopen(fileName, "r");

	if (fp == NULL){
		perror("Error");
		exit(EXIT_FAILURE);
	}

    /* break loop with ^Z or ^D */
    while (fgets(line, sizeof(line)/sizeof(char), fp)) {
        char token, type;
        char name[MAX_INPUT_SIZE];

        int numTokens = sscanf(line, "%c %s %c", &token, name, &type);

        /* perform minimal validation */
        if (numTokens < 1) {
            continue;
        }
        switch (token) {
            case 'c':
                if(numTokens != 3)
                    errorParse();
                if(insertCommand(line))
                    break;
                return;
            
            case 'l':
                if(numTokens != 2)
                    errorParse();
                if(insertCommand(line))
                    break;
                return;
            
            case 'd':
                if(numTokens != 2)
                    errorParse();
                if(insertCommand(line))
                    break;
                return;
            
            case '#':
                break;
            
            default: { /* error */
                errorParse();
            }
        }
    }
    fclose(fp);
}

void applyCommands(){
	while(1){
        const char* command;

        if (syncStrategy != 'n'){
            pthread_mutex_lock(&commandLock);
            if (numberCommands > 0){
            	command = removeCommand();
            	if (command == NULL){
            		pthread_mutex_unlock(&commandLock);
            		continue;
            	} else
            		pthread_mutex_unlock(&commandLock);
            }
            else{
            	pthread_mutex_unlock(&commandLock);
            	return;
            }           
        } else{
        	if (numberCommands > 0)
            	command = removeCommand();
            else
            	return;
            if (command == NULL)
            	continue;
        }
        

        char token, type;
        char name[MAX_INPUT_SIZE];
        int numTokens = sscanf(command, "%c %s %c", &token, name, &type);
        if (numTokens < 2) {
            fprintf(stderr, "Error: invalid command in Queue\n");
            exit(EXIT_FAILURE);
        }


        int searchResult;
        switch (token) {
            case 'c':
                switch (type) {
                    case 'f':
                        lock('w');
                        printf("Create file: %s\n", name);
                        create(name, T_FILE);
                        unlock();
                        break;

                    case 'd':
                        lock('w');
                        printf("Create directory: %s\n", name);
                        create(name, T_DIRECTORY);
                        unlock();
                        break;

                    default:
                        fprintf(stderr, "Error: invalid node type\n");
                        exit(EXIT_FAILURE);
                }
                break;

            case 'l': 
                lock('r');
                searchResult = lookup(name);
                if (searchResult >= 0)
                    printf("Search: %s found\n", name);
                else
                    printf("Search: %s not found\n", name);
                unlock();
                break;

            case 'd':
                lock('w');
                printf("Delete: %s\n", name);
                delete(name);
                unlock();
                break;

            default: { /* error */
                fprintf(stderr, "Error: command to apply\n");
                exit(EXIT_FAILURE);
            }
        }
    }
}

void *fnThread(void *arg){
  applyCommands();
  return NULL;
}

int main(int argc, char* argv[]){
    //VERIFICACOES

    //verificar numero de argumentos
	if (argc < 5){
        fprintf(stderr, "Error: too few arguments\n");
    	exit(EXIT_FAILURE);
    }

    int threads = atoi(argv[3]);

    //verificar que numthreads e um inteiro maior que 0
    if (threads < 1){
        fprintf(stderr, "Error: invalid number of threads\n");
        exit(EXIT_FAILURE);
    }

    //verificar que synchstrategy e uma das 3 opcoes validas
    if (strcmp(argv[4], "mutex") && strcmp(argv[4], "rwlock") && strcmp(argv[4], "nosync")){
        fprintf(stderr, "Error: synchstrategy invalid\n");
        exit(EXIT_FAILURE);
    }

    //verificar que quando synchstrategy e nosync numthreads e igual a 1
    if (!strcmp(argv[4], "nosync") && threads != 1){
        fprintf(stderr, "Error: nosync is only valid with 1 thread (numthreads = 1)\n");
        exit(EXIT_FAILURE);
    }

    //verificar que quando numthreads e igual a 1 synchstrategy e nosync
    if (strcmp(argv[4], "nosync") && threads == 1){
        fprintf(stderr, "Error: when numthreads = 1, nosync is the only valid strategy\n");
        exit(EXIT_FAILURE);
    }


    //INICIO CODIGO

    FILE* fp = fopen(argv[2], "w");

    //verificar que o ficheiro existe
	if (fp == NULL){
		perror("Error");
		exit(EXIT_FAILURE);
	}

    syncStrategy = argv[4][0];

    //inicializar mutex global
    if (syncStrategy != 'n' && pthread_mutex_init(&commandLock, NULL) != 0){
        perror("Error");
        exit(EXIT_FAILURE);
    }

    //inicializar mutex global
    if (syncStrategy == 'm' && pthread_mutex_init(&globalLock, NULL) != 0){
        perror("Error");
        exit(EXIT_FAILURE);
    }

    //inicializar rwlock
    if (syncStrategy == 'r' && pthread_rwlock_init(&rwlock, NULL) != 0){
        perror("Error");
        exit(EXIT_FAILURE);
    }

    



    /* init filesystem */
    init_fs();

    /* process input and print tree */
    processInput(argv[1]);

	/* iniciar pool de tarefas */
    pthread_t tid[threads];

    for (int i = 0; i < threads; i++) {
        if (pthread_create (&tid[i], NULL, fnThread, NULL) != 0){
            fprintf(stderr, "Error: can't create thread\n");
            exit(EXIT_FAILURE);
        }
    }

	// ir buscar hora de inicio 
    struct timeval start, stop;
	gettimeofday(&start, NULL);
    
    for (int i = 0; i < threads; i++) {
        if (pthread_join (tid[i], NULL) != 0){
            fprintf(stderr, "Error: can't join thread\n");
            exit(EXIT_FAILURE);
        }
    }

    //ir buscar hora de fim
	gettimeofday(&stop, NULL);
	
    //subtrair tempo inicial ao tempo final para obter tempo de execucao
    double secs = (double)(stop.tv_usec - start.tv_usec) / 1000000 + (double)(stop.tv_sec - start.tv_sec);
	printf("TecnicoFS completed in %.4f seconds.\n",secs);

    print_tecnicofs_tree(fp);
    fclose(fp);

    /* release allocated memory */
    destroy_fs();
    destroyLocks();
    exit(EXIT_SUCCESS);
}
