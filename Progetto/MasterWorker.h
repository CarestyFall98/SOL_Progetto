#ifndef MASTERWORKER_H
#define MASTERWORKER_H

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
#include <sys/socket.h>
#include <sys/un.h>

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

#endif