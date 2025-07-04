#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <netdb.h>

#define PORT_DEFAULT 8080
#define BUFFER_SIZE 1024

int main() {
    int sock;
    char buffer[BUFFER_SIZE];
    struct addrinfo hints, *servinfo, *p;
    int status;

    // Lettura delle variabili d'ambiente per host e porta del server
    char *server_host_env = getenv("SERVER_HOST");
    char *server_port_str_env = getenv("SERVER_PORT");
    char *server_host;
    char server_port_str[6]; // porta come stringa (max 5 cifre + terminatore)

    // Default host: "127.0.0.1"
    if (server_host_env == NULL) {
        server_host = "127.0.0.1";
        fprintf(stderr, "SERVER_HOST non specificato, uso default: %s\n", server_host);
    } else {
        server_host = server_host_env;
    }

    // Default port: 8080
    if (server_port_str_env == NULL) {
        snprintf(server_port_str, sizeof(server_port_str), "%d", PORT_DEFAULT);
        fprintf(stderr, "SERVER_PORT non specificato, uso default: %s\n", server_port_str);
    } else {
        snprintf(server_port_str, sizeof(server_port_str), "%s", server_port_str_env);
    }

    // Preparazione per risoluzione indirizzo con getaddrinfo
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;     // IPv4 o IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP

    status = getaddrinfo(server_host, server_port_str, &hints, &servinfo);
    if (status != 0) {
        fprintf(stderr, "Errore getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    // Tentativo di connessione al server
    for (p = servinfo; p != NULL; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == -1) {
            perror("Errore nella creazione del socket");
            continue;
        }

        if (connect(sock, p->ai_addr, p->ai_addrlen) == -1) {
            perror("Connessione fallita");
            close(sock);
            continue;
        }

        break;
    }

    // Se non è stato possibile connettersi a nessun indirizzo
    if (p == NULL) {
        fprintf(stderr, "Impossibile connettersi a %s:%s\n", server_host, server_port_str);
        freeaddrinfo(servinfo);
        exit(EXIT_FAILURE);
    }

    // Connessione riuscita
    printf("Connesso al server Tris su %s:%s\n", server_host, server_port_str);
    fflush(stdout);
    freeaddrinfo(servinfo);

    // Preparazione per multiplexing con select()
    fd_set master_fds, read_fds;
    int max_fd = sock;

    FD_ZERO(&master_fds);
    FD_SET(STDIN_FILENO, &master_fds); // input da tastiera
    FD_SET(sock, &master_fds);         // socket server

    // Loop principale: gestisce input utente e messaggi dal server
    while (1) {
        read_fds = master_fds;

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("Errore select");
            break;
        }

        // Messaggio dal server
        if (FD_ISSET(sock, &read_fds)) {
            int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) {
                printf("Server disconnesso.\n");
                break;
            }
            buffer[bytes_received] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        }

        // Input da tastiera
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
                send(sock, buffer, strlen(buffer), 0);
            }
        }
    }

    close(sock);
    return 0;
}