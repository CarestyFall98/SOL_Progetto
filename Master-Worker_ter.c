#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <getopt.h>
#include <dirent.h>
#include <signal.h>

#include "utils/includes/util.h"
#include "utils/includes/conn.h" //Ho riciclato gli header files delle soluzioni degli esercizi di lab

#define NAME_MAX_LEN 255

volatile int is_running = 1;

typedef struct FileNames{
    char *filename;
    struct FileNames *next;
} file_names;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_insert = PTHREAD_COND_INITIALIZER;
pthread_cond_t c_remove = PTHREAD_COND_INITIALIZER;

pthread_mutex_t mux_sock = PTHREAD_MUTEX_INITIALIZER;

static int nthreads = 4, qlen = 8, time_delay = 0; // Variabili globali con i valori di default date dal testo del progetto
static int active_threads = 0;
static file_names *Names = NULL; // Puntatore alla testa della lista dei file names
static pthread_t *pool;

static int num_files = 0; // Numero dei files presenti nella lista inizialmente
static char *dir;         // Nome della directory

static char *finished = "continue";

static int notused;
static int sockfd;

volatile sig_atomic_t end = 1;   // Variabile che mi dice se devo terminare, dopo aver terminato le richieste
volatile sig_atomic_t print = 1; // Variabile che dice se devo stampare i file temporanei

void sig_handler(int signum){
    switch (signum){

        case SIGHUP:
            end = 1;
            break;
        case SIGINT:
            end = 1;
            break;
        case SIGQUIT:
            end = 1;
            break; 
        case SIGTERM:
            end = 1;
            break; 
        case SIGUSR1:
        printf("Ricevuto SIGUSR1\n");
            print = 1;
            char *stampa = "stampa\0";
            LOCK(&mux_sock);
            write(sockfd, stampa, NAME_MAX_LEN);
            UNLOCK(&mux_sock);
            break;  
    }
}

// Aggiungo un nome di un file all'array

void insert_name(char *filename){
    LOCK(&mutex);

    while (num_files == qlen){
        WAIT(&c_insert, &mutex);
    }

    if (Names == NULL){
        Names = malloc(sizeof(file_names));
        Names->filename = malloc(NAME_MAX_LEN);
        strncpy(Names->filename, filename, NAME_MAX_LEN);
        Names->next = NULL;
        num_files++;
        SIGNAL(&c_remove);
        UNLOCK(&mutex);
        return;
    }

    file_names *add = malloc(sizeof(file_names));
    add->filename = malloc(NAME_MAX_LEN);
    strncpy(add->filename, filename, NAME_MAX_LEN);
    add->next = NULL;

    file_names *curr = Names;
    while (curr->next != NULL){
        curr = curr->next;
    }
    curr->next = add;
    num_files++;

    SIGNAL(&c_remove);
    UNLOCK(&mutex);
}

// Tolgo un nome di un file dall'array

void remove_name(void){

    file_names *curr = Names;
    Names = Names->next;
    free(curr);
    num_files--;

    SIGNAL(&c_insert);
}

// Inserisco il nome dalla directory

void from_directory(char *directory, char *tmp, char *d_path, struct dirent *entry, DIR *dir){

    if (end)
        return;

    if (directory == NULL)
        return;

    if (!(dir = opendir(directory)))
        return;

    while ((entry = readdir(dir)) != NULL){
        if (entry->d_type != DT_DIR){
            if (strstr(entry->d_name, ".dat")){
                strcat(tmp, directory);
                strcat(tmp, "/");
                strcat(tmp, entry->d_name);

                sleep(time_delay);
                insert_name(tmp);
                strcpy(tmp, "\0");
            }
        }
        else if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0){ // Ricorsivo
            sprintf(d_path, "%s/%s", directory, entry->d_name);
            from_directory(d_path, tmp, d_path, entry, dir);
        }
    }
}

// Funzione di lettura e calcolo dei long dei file
long read_file(char *filename){

    FILE *fd; // Descrittore del file

    if ((fd = fopen(filename, "rb")) == NULL){
        fprintf(stderr, "\nErrore durante l'apertura del file.\n"); // Gestione dell'errore all'apertura del file
        return EXIT_FAILURE;
    }

    fseek(fd, 0, SEEK_END);
    long file_size = ftell(fd);
    fseek(fd, 0, SEEK_SET);

    int nelem = file_size / sizeof(long);

    long *array = (long *)malloc(nelem * sizeof(long));
    if (array == NULL){
        fprintf(stderr, "\nErrore durante l'allocazione di memoria.\n");
        return EXIT_FAILURE;
    }
    fread(array, sizeof(long), nelem, fd);
    fclose(fd);

    long result = 0;

    for (int i = 0; i < nelem; i++){
        result += i * array[i];
    }

    free(array);

    return result;
}

//Creazione Threadpool

void threadpool(){
    pool = malloc(sizeof(pthread_t));

    for (int i = 0; i < nthreads; i++){
        active_threads++;
        pthread_create(&pool[i], NULL, worker, NULL);
    }
    
}

//Funzione worker

void *worker(){
    while (1){
        LOCK(&mutex);

        while (num_files == 0){
            WAIT(&c_remove, &mutex);
        }

        if (!strcmp(Names->filename, "File terminati.")){
            printf("Tutti i file sono stati prelevati");
            finished = "stop\0";
            active_threads--;
            UNLOCK(&mutex);
            return NULL;
        }
        
        char *file = Names->filename; //Prelevo il dato dalla coda e lo rimuovo
        remove_name();

        UNLOCK(&mutex);

        long result = read_file(file);

        if (result == -1){
            free(file);
            continue;
        }

        LOCK(&mux_sock);
        SYSCALL_EXIT("writen", notused, writen(sockfd, file, NAME_MAX_LEN), "writen", "");

        sprintf(file, "%lu", result);

        SYSCALL_EXIT("writen", notused, writen(sockfd, file, NAME_MAX_LEN), "writen", "");

        SYSCALL_EXIT("readn", notused, readn(sockfd, file, sizeof(int)), "readn", "");

        UNLOCK(&mux_sock);
        free(file);
    }
    return NULL;
}

int main(int argc, char *argv[]){

    int d_flag = 0;

    struct option long_options[] = {
        {"nthreads", required_argument, NULL, 'n'},
        {"qlen", required_argument, NULL, 'q'},
        {"dir", no_argument, NULL, 'd'},
        {"time_sleep", required_argument, NULL, 't'},
        {0, 0, 0, 0}};

    int opt;
    char *endptr;

    while ((opt = getopt_long(argc, argv, "n:q:d:t:", long_options, NULL)) != -1){
        // Faccio il il parsing degli argomenti passati da linea di comando con la funzione getopt()
        switch (opt){
        case 'n': // Numero dei threads
            isNumber(optarg, (long *)&nthreads);
            nthreads = strtol(optarg, &endptr, 10);
            if (*endptr != '\0' && *endptr != '\n'){
                fprintf(stderr, "Invalid number for '-n'\n");
                return 1;
            }
            break;
        case 'q': // Lunghezza dell'array circolare
            isNumber(optarg, (long *)&qlen);
            qlen = strtol(optarg, &endptr, 10);
            if (*endptr != '\0' && *endptr != '\n'){
                fprintf(stderr, "Invalid number for '-q'\n");
                return 1;
            }
            break;
        case 'd': // Scansione della directory
            d_flag = 1;
            dir = optarg;
            break;
        case 't': // Tempo di attesa in millisecondi
            isNumber(optarg, (long *)&time);
            time_delay = strtol(optarg, &endptr, 10);
            if (*endptr != '\0' && *endptr != '\n'){
                fprintf(stderr, "Invalid number for '-t'\n");
                return 1;
            }
            break;

        default:
            fprintf(stderr, "\nTroppi pochi argomenti. Usa [-opzione] [argomento]\n");
            return 1;
        }
    }

    printf("Number of threads (-n): %d\n", nthreads);
    printf("Array size (-q): %d\n", qlen);
    printf("Directory flag (-d): %s\n", d_flag ? "true" : "false");
    printf("Sleep time (-t): %d milliseconds\n", time_delay);

     struct sigaction sa;
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGUSR1, sig_handler);

    // Sleep for the specified time
    if (time_delay >= 0){
        printf("\nSleeping for %d milliseconds...\n\n", time_delay);
        struct timespec delay;
        delay.tv_sec = time_delay / 1000;
        delay.tv_nsec = (time_delay % 1000) * 1000000;
        while (is_running && nanosleep(&delay, &delay) == -1 && errno == EINTR);
    }

    struct dirent *entry = NULL;
    char tmp[NAME_MAX_LEN] = "\0";
    char d_path[NAME_MAX_LEN] = "\0";
    DIR *d = NULL;

    if (d_flag == 1){
        from_directory(dir, tmp, d_path, entry, d);
    }

    int i = 0;

    while (i < argc && end){
        if (end)
            break;
        if (strstr(argv[i], ".dat")){
            sleep(time_delay);
            insert_name(argv[i]);
        }
        exit(EXIT_FAILURE);
    }

    //Implemento la socket
    struct sockaddr_un serv_addr;
    SYSCALL_EXIT("socket", notused, socket(AF_UNIX, SOCK_STREAM, 0), "socket", "");

    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sun_family = AF_UNIX;
    strncpy(serv_addr.sun_path, SOCKNAME, strlen(SOCKNAME) + 10);

    int pid_c; //PID collector

    pid_c = fork();

    if (pid_c == 0){
        collector();
    }
    else {
        
    }

   
   // for (i = optind; i < argc; i++){
   //     l = read_file(argv[i]);
   // }
   // printf("Il risultato e': %ld\n", l);
    // CircArray fileArray;
    // init_array(&fileArray, qlen);

    /*
    pthread_t* worker_threads = (pthread_t *)malloc(nthreads * sizeof(pthread_t));
    //int* worker_ids = (int*)malloc(nthreads * sizeof(int));

    if (pthread_mutex_init(&fileArray.mutex, NULL) != 0) {
        perror("Inizializzazione della mutex fallita");
        exit(EXIT_FAILURE);
    }

    if (pthread_cond_init(&fileArray.empty, NULL) != 0 || pthread_cond_init(&fileArray.full, NULL) != 0) {
        perror("Inizializzazione della condition variable fallita");
        exit(EXIT_FAILURE);
    }
    */
    /**
     * fileArray.size = qlen;
     * fileArray.current_files = 0;
     * fileArray.index = 0;
     * fileArray.terminated = 0;
     */

    // Aggiungo i nomi dei file all'array
    /* for (int i = optind; i < argc; i++) {
         insert(&fileArray, argv[i]);
     }

     for (int i = 0; i < nthreads; i++) {
         if (pthread_create(&worker_threads[i], NULL, worker, &fileArray) != 0) {
             fprintf(stderr, "Errore nella creazione dei thread worker.");
             exit(EXIT_FAILURE);
         }
     }

     //finito = 1; //Se ho finito lo spazio nell'array

     //pthread_cond_broadcast(&fileArray.empty);

     //Attendo il completamento dei thread worker

    for (int i = 0; i < nthreads; i++) {
         char empty_filename[NAME_MAX_LENGTH] = "";
         insert(&fileArray, empty_filename); //Segnala ai worker di terminare
     }


     for (int i = 0; i < nthreads; i++) {
         pthread_join(worker_threads[i], NULL);
     }


     free(worker_threads);
     for (int i = 0; i < qlen; i++) {
         free(fileArray.filenames[i]);
     }
     free(fileArray.filenames);

      //Termina il programma
     pthread_mutex_destroy(&fileArray.mutex);
     pthread_cond_destroy(&fileArray.full);
     pthread_cond_destroy(&fileArray.empty);
    */
    return 0;
}
