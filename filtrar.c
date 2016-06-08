#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "filtrar.h"

extern void apply_filter(char* filter_name);
extern void prepare_alarm(void);

void prepare_filters(void);
void wait_termination(void);
void walk_directory(char* dir_name);

char** filters;
int    num_filters;
pid_t* pids;

const char* END_PROC_CODE = "%s: %d\n";
const char* END_PROC_SIGNAL = "%s: senyal %d\n";

const char* ERR_CREATE_PIPE = "Error al crear el pipe\n";
const char* ERR_CREATE_PROC = "Error al crear proceso %d\n";
const char* ERR_EXEC_FILTER = "Error al ejecutar el filtro '%s'\n";
const char* ERR_EXEC_PROC = "Error al ejecutar el mandato '%s'\n";
const char* ERR_FIND_SYMBOL = "Error al buscar el simbolo 'tratar' en '%s'\n";
const char* ERR_KILL_PROC = "Error al intentar matar proceso %d\n";
const char* ERR_OPEN_DIR = "Error al abrir el directorio '%s'!\n";
const char* ERR_OPEN_LIB = "Error al abrir la biblioteca '%s'\n";
const char* ERR_READ_DIR = "Error al leer el directorio '%s'\n";
const char* ERR_TIMEOUT_FORMAT = "Error FILTRAR_TIMEOUT no es entero positivo: '%s'\n";
const char* ERR_WAIT_PROC = "Error al esperar proceso %d\n";

const char* MSG_ALARM_ON = "AVISO: La alarma ha saltado!\n";
const char* MSG_ALARM_READY = "AVISO: La alarma vencera tras %d segundos!\n";
const char* MSG_OPEN_FILE = "AVISO: No se puede abrir el fichero '%s'!\n";
const char* MSG_STAT_FILE = "AVISO: No se puede stat el fichero '%s'!\n";
const char* MSG_USAGE = "Uso: %s directorio [filtro...]\n";

#define MAX_FILE_NAME 1024
#define MAX_FILE_SIZE 4096

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, MSG_USAGE, argv[0]);
    exit(1);
  }
  filters = &(argv[2]);
  num_filters = argc - 2;
  pids = (pid_t*) malloc(sizeof(pid_t) * num_filters);
  prepare_alarm();
  prepare_filters();
  walk_directory(argv[1]);
  wait_termination();
  return 0;
}

void alarm_handler() {
  int i;
  fprintf(stderr, "%s", MSG_ALARM_ON);
  // Enviar señales para matar a los hijos.
  for (i = 0; i < num_filters; i++) {
    if (kill(pids[i], 0) == 1) {
      if ((kill(pids[i], SIGKILL)) < 0) {
        fprintf(stderr, ERR_KILL_PROC, pids[i]);
        exit(1);
      }
    }
  }
}

void apply_filter(char *filter_name) {
  char buffer_in[MAX_FILE_SIZE], buffer_out[MAX_FILE_SIZE];
  void *library;
  int  aux, num_bytes;
  int (*filter) (char*, char*, int);
  library = dlopen(filter_name, RTLD_LAZY);
  if (library == NULL) {
    fprintf(stderr, ERR_OPEN_LIB, filter_name);
    exit(1);
  }
  filter = dlsym(library, "tratar");
  if (filter == NULL) {
    fprintf(stderr, ERR_FIND_SYMBOL, filter_name);
    exit(1);
  }
  while ((num_bytes = read(0, buffer_in, MAX_FILE_SIZE)) > 0) {
    aux = filter(buffer_in, buffer_out, num_bytes);
    write(1, buffer_out, aux);
    if (aux < 0) {
      fprintf(stderr, ERR_EXEC_FILTER, filter_name);
      exit(1);
    }
  }
  dlclose(library);
}

int is_positive_number(char *str) {
  int i;
  for (i = 0; i < strlen(str); i++) {
    if (!isdigit(str[i])) {
      return 0;
    }
  }
  return 1;
}

void prepare_alarm() {
  // Sacar la variable de entorno.
  char *timeout_str = getenv("FILTRAR_TIMEOUT");
  if (timeout_str == NULL) {
    return;
  }
  // Comprobar que es un entero positivo.
  if (!is_positive_number(timeout_str)) {
    fprintf(stderr, ERR_TIMEOUT_FORMAT, timeout_str);
    exit(1);
  }
  int timeout = atoi(timeout_str);
  fprintf(stderr, MSG_ALARM_READY, timeout);
  // Armar la señal.
  struct sigaction act;
  act.sa_handler = &alarm_handler;
  act.sa_flags = SA_RESTART;
  sigaction(SIGALRM, &act, NULL);
  alarm(timeout);
}

void prepare_filters(void) {
  int  i, proc;
  char *file_name;
  for (i = 0; i < num_filters; i++) {
    int pp[2];
    if (pipe(pp) < 0) {
      fprintf(stderr, "%s", ERR_CREATE_PIPE);
      exit(1);
    }
    switch (proc = fork()) {
      case -1:
        fprintf(stderr, ERR_CREATE_PROC, proc);
        exit(1);
      // Proceso hijo que realiza el filtrado.
      case 0:
        dup2(pp[0], 0);
        close(pp[0]);
        close(pp[1]);
        file_name = strrchr(filters[i], '.');
        if (file_name != NULL && strcmp(file_name, ".so") == 0) {
          apply_filter(filters[i]);
          exit(0);
        }
        execlp(filters[i], filters[i], NULL, NULL);
        // La ejecución solamente se devuelve si el mandato falla.
        fprintf(stderr, ERR_EXEC_PROC, filters[i]);
        exit(1);
      default:
        // Redirección del proceso padre.
        close(pp[0]);
        dup2(pp[1], 1);
        close(pp[1]);
    }
  }
}

void print_status(char *filter_name, int status) {
  if (WIFEXITED(status)) {
    // Código de terminación del proceso.
    fprintf(stderr, END_PROC_CODE, filter_name, WEXITSTATUS(status));
  } else {
    // Muestra la señal que ha matado al proceso.
    fprintf(stderr, END_PROC_SIGNAL, filter_name, WTERMSIG(status));
  }
}

void wait_termination(void) {
  int i, status;
  close(1);
  for (i = 0; i < num_filters; i++) {
    if (waitpid(pids[i], &status, 0) < 0) {
      fprintf(stderr, ERR_WAIT_PROC, pids[i]);
      exit(1);
    }
    print_status(filters[i], status);
  }
}

void walk_directory(char* dir_name) {
  DIR*   dir = NULL;
  struct dirent* ent;
  char   file_name[MAX_FILE_NAME];
  char   file_content[MAX_FILE_SIZE];
  int    fd, num_bytes;
  struct stat status;
  // Apertura del directorio.
  dir = opendir(dir_name);
  if (dir == NULL) {
    fprintf(stderr, ERR_OPEN_DIR, dir_name);
    exit(1);
  }
  // Recorrer entradas del directorio.
  while((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] == '.') {
      continue;
    }
    strcpy(file_name, dir_name);
    strcat(file_name, "/");
    strcat(file_name, ent->d_name);
    if (stat(file_name, &status) < 0) {
      fprintf(stderr, MSG_STAT_FILE, file_name);
      exit(0);
    }
    if (S_ISDIR(status.st_mode)) {
      continue;
    }
    // Abrir cada archivo y tratamiento del error.
    fd = open(file_name, O_RDONLY);
    if (fd == -1) {
      fprintf(stderr, MSG_OPEN_FILE, file_name);
      exit(0);
    }
    // Emitir contenido por la salida estándar.
    num_bytes = read(fd, file_content, MAX_FILE_SIZE);
    write(1, file_content, num_bytes);
    // Cerrar los descriptores.
    close(fd);
    // Reiniciar el contador de errores.
    errno = 0;
  }
  // Tratar el error al leer el directorio.
  if (errno) {
    fprintf(stderr, ERR_READ_DIR, dir_name);
    exit(1);
  }
  // Cerrar el directorio.
  closedir(dir);
}
