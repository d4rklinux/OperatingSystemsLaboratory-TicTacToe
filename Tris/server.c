#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h> 
#include <stdbool.h>
#include <errno.h> 

#include "tris_game.h" // Include il file di intestazione per la logica del gioco del tris

#define PORT 8080
#define MAX_CLIENTS 256
#define BUFFER_SIZE 1024
#define MAX_GAMES 128

// Enumerazione per lo stato di un giocatore
typedef enum {
    PLAYER_CONNECTED,     // Connesso ma non in partita
    PLAYER_IN_GAME,       // In una partita
    PLAYER_WAITING_ACCEPT // Ha richiesto di unirsi ad una partita, in attesa di accettazione
} PlayerStatus;

// Enumerazione per lo stato della partita estesa
typedef enum {
    GAME_NEW,              // Partita appena creata, in attesa del proprietario per iniziare
    GAME_WAITING_FOR_PLAYER, // Proprietario pronto, in attesa di un secondo giocatore
    GAME_IN_PROGRESS,      // Partita in corso
    GAME_ENDED            // Partita terminata
} GameState;

// Struttura per rappresentare un giocatore
typedef struct {
    int fd;                 // File descriptor del socket del client
    PlayerStatus status;    // Stato attuale del giocatore
    char username[32];      // Nome utente (opzionale)
    int game_id;            // ID della partita a cui il giocatore appartiene (-1 se non in partita)
    Cell player_symbol;     // Simbolo del giocatore (X o O)
    bool is_current_turn;   // Vero se è il turno di questo giocatore
    bool wants_rematch;     // Vero se il giocatore ha chiesto una rivincita (dopo un pareggio)
    int wins;              // Numero di vittorie del giocatore (per statistiche)
    int losses;            // Numero di sconfitte del giocatore (per statistiche)
    int draws;             // Numero di pareggi del giocatore (per statistiche)
} Client;

// Struttura per rappresentare una partita
typedef struct {
    int id;                 // ID univoco della partita (-1 se slot libero)
    GameState state;        // Stato attuale della partita
    int owner_fd;           // File descriptor del creatore della partita
    int opponent_fd;        // File descriptor del secondo giocatore (-1 se non c'è)
    TrisGame tris_game;     // Stato del gioco del tris
    GameResult last_result; // Risultato dell'ultima partita (WIN, DRAW, IN_PROGRESS)
} Game;

// --- Variabili Globali ---
Client clients[MAX_CLIENTS]; // Array di client connessi
Game games[MAX_GAMES]; // Array di partite attive

int num_clients = 0; // Numero di client connessi
int num_games = 0; // Numero di partite attive
int next_game_id = 1; // ID univoco per le partite
int next_player_number = 1; // Contatore per assegnare nomi unici ai giocatori

fd_set master_fds; // Set di descrittori di file master per select()
int max_sd;        // Massimo descrittore di file nel set per select()

// --- Prototipi delle Funzioni ---
void send_to_client(int client_fd, const char *message); // Funzione per inviare messaggi ai client
void initialize_client(int client_fd); // Funzione per inizializzare un nuovo client
void cleanup_game(Game *game); // Funzione per pulire una partita, rendendola disponibile
void remove_client_from_game(int client_fd); // Rimuove un client da qualsiasi partita in cui si trova
void remove_client(int client_fd); // Rimuove un client dal server (disconnessione completa)
Game* find_game_by_id(int game_id); // Trova una partita tramite il suo ID
Game* find_game_by_player_fd(int player_fd); // Trova una partita a cui è associato un giocatore
Client* find_client_by_fd(int client_fd); // Trova un client tramite il suo file descriptor
void print_game_list(int client_fd); // Stampa la lista delle partite disponibili a un client
void broadcast_to_lobby(Game *game, const char *message); // Invia un messaggio a tutti i client nella lobby
void send_game_state_to_players(Game *game); // Invia lo stato attuale del tabellone e il turno ai giocatori della partita

// Prototipi per la gestione dei comandi
void handle_create_command(int client_fd, Client *current_client); // Gestisce il comando "create"
void handle_join_command(int client_fd, Client *current_client, const char *buffer); // Gestisce il comando "join"
void handle_accept_command(int client_fd, Client *current_client); // Gestisce il comando "accept"
void handle_reject_command(int client_fd, Client *current_client); // Gestisce il comando "reject"
void handle_leave_command(int client_fd, Client *current_client); // Gestisce il comando "leave"
void handle_move_command(int sd, const char* buffer); // Gestisce il comando "move" e aggiorna la statistica del giocatore
void handle_rematch_command(int sd); // Gestisce il comando "rematch" per richiedere una rivincita
void handle_client_data(int sd, char *buffer, int valread); // Gestisce i dati ricevuti da un client
void handle_statistics_command(int client_fd); // Gestisce il comando "statistics" per visualizzare le statistiche dei giocatori

// --- Implementazioni delle Funzioni di Utilità ---

/**
 * @brief Invia un messaggio a un client specifico.
 * @param client_fd Il file descriptor del client.
 * @param message Il messaggio da inviare.
 */
void send_to_client(int client_fd, const char *message) {
    if (client_fd > 0) {
        if (send(client_fd, message, strlen(message), 0) == -1) {
            perror("send");
        }
    }
}

/**
 * @brief Inizializza una nuova struttura Client per un client connesso.
 * @param client_fd Il file descriptor del nuovo client.
 */
void initialize_client(int client_fd) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd == 0) { // Trova uno slot libero
            clients[i].fd = client_fd; // Assegna il file descriptor
            clients[i].status = PLAYER_CONNECTED; // Stato iniziale del client
            clients[i].game_id = -1; // Non in partita inizialmente
            clients[i].player_symbol = EMPTY; // Simbolo iniziale vuoto
            clients[i].is_current_turn = false; // Non è il turno del client
            clients[i].wants_rematch = false; // Non ha richiesto una rivincita
            clients[i].wins = 0; // Inizializza il conteggio delle vittorie
            clients[i].losses = 0; // Inizializza il conteggio delle sconfitte
            clients[i].draws = 0; // Inizializza il conteggio dei pareggi
            snprintf(clients[i].username, sizeof(clients[i].username), "Giocatore%d", next_player_number++);            
            printf("Nuovo client connesso: FD %d. Totale client: %d\n", client_fd, num_clients);
            send_to_client(client_fd, "\nBenvenuto al gioco del Tris (Tic-Tac-Toe)!\n\n");
            send_to_client(client_fd, "Comandi disponibili:\n");
            send_to_client(client_fd, "  create - Crea una nuova partita\n");
            send_to_client(client_fd, "  join <game_id> - Unisciti a una partita esistente\n");
            send_to_client(client_fd, "  list - Elenca le partite disponibili\n");
            send_to_client(client_fd, "  leave - Lascia la partita corrente\n");
            send_to_client(client_fd, "  move <row> <col> - Effettua una mossa (es. move 0 0)\n");
            send_to_client(client_fd, "  statistics - Visualizza le statistiche dei giocatori\n"); 
            send_to_client(client_fd, "  quit - Disconnettiti dal server\n");
            return;
        }
    }
    send_to_client(client_fd, "Server pieno, riprova più tardi.\n");
    close(client_fd);
}

/**
 * @brief Resetta uno slot di gioco, rendendolo disponibile.
 * @param game Puntatore alla struttura Game da pulire.
 */
void cleanup_game(Game *game) {
    if (game) {
        printf("Pulizia partita ID: %d\n", game->id);
        game->id = -1; // Indica che lo slot è libero
        game->state = GAME_NEW; // Stato iniziale
        game->owner_fd = -1; // Nessun proprietario
        game->opponent_fd = -1; // Nessun avversario
        game->last_result = IN_PROGRESS; // Resetta il risultato
        // Non è necessario pulire tris_game, verrà reinizializzato alla creazione
        num_games--;
        if (num_games < 0) num_games = 0; // Sicurezza per evitare conteggio negativo
    }
}

/**
 * @brief Rimuove un client da qualsiasi partita in cui si trovi, riportandolo a PLAYER_CONNECTED.
 * @param client_fd Il file descriptor del client.
 */
void remove_client_from_game(int client_fd) {
    Client* client_to_remove = find_client_by_fd(client_fd);
    if (!client_to_remove || client_to_remove->game_id == -1) {
        return; // Client non trovato o non in gioco
    }

    Game* game = find_game_by_id(client_to_remove->game_id);
    if (!game) {
        // Partita non trovata, stato inconsistente. Resetta solo il client.
        printf("ATTENZIONE: Partita ID %d per client %d non trovata durante rimozione da gioco.\n", client_to_remove->game_id, client_fd);
        client_to_remove->game_id = -1; 
        client_to_remove->status = PLAYER_CONNECTED; 
        client_to_remove->player_symbol = EMPTY;
        client_to_remove->is_current_turn = false;
        client_to_remove->wants_rematch = false;
        return;
    }

    // Se il client che sta uscendo è l'avversario
    if (game->opponent_fd == client_to_remove->fd) {
        game->opponent_fd = -1; // Rimuovi l'avversario
        // Se c'è un proprietario, notifica e imposta la partita in attesa
        if (game->owner_fd != -1) {
            send_to_client(game->owner_fd, "Il tuo avversario ha lasciato la partita. La partita è ora in attesa di un nuovo giocatore.\n");
            game->state = GAME_WAITING_FOR_PLAYER;
            printf("Partita %d: l Avversario FD %d lasciato, proprietario FD %d ora in attesa.\n", game->id, client_to_remove->fd, game->owner_fd);
        } else {
            // Se non c'è più neanche il proprietario, la partita è vuota, puliscila
            cleanup_game(game);
        }
    }
    // Se il client che sta uscendo è il proprietario
    else if (game->owner_fd == client_to_remove->fd) {
        // Se c'è un avversario, notifica e pulisci la partita
        if (game->opponent_fd != -1) {
            send_to_client(game->opponent_fd, "Il proprietario della partita ha lasciato. La partita è terminata per mancanza di giocatori.\n");
            Client* other_player = find_client_by_fd(game->opponent_fd);
            if (other_player) {
                other_player->game_id = -1;
                other_player->status = PLAYER_CONNECTED;
                other_player->player_symbol = EMPTY;
                other_player->is_current_turn = false;
                other_player->wants_rematch = false;
            }
        }
        printf("Partita %d: Proprietario FD %d lasciato. Partita pulita.\n", game->id, client_to_remove->fd);
        cleanup_game(game); // Pulisci la partita
    } else {
        // Client in gioco ma non proprietario né avversario (es. in stato di accettazione ma non matchato)
        printf("Client FD %d non era proprietario né avversario in partita %d, ma era associato. Dissocia.\n", client_to_remove->fd, client_to_remove->game_id);
    }

    // Resetta lo stato del client che ha lasciato la partita
    client_to_remove->game_id = -1;
    client_to_remove->status = PLAYER_CONNECTED;
    client_to_remove->player_symbol = EMPTY;
    client_to_remove->is_current_turn = false;
    client_to_remove->wants_rematch = false;

    printf("Client FD %d rimosso dalla partita (stato resettato).\n", client_fd);
}

/**
 * @brief Rimuove un client dal server (disconnessione completa).
 * @param client_fd Il file descriptor del client da rimuovere.
 */
void remove_client(int sd) {
    // Prima, rimuovi il client da qualsiasi partita
    remove_client_from_game(sd);

    close(sd); // Chiude il socket
    FD_CLR(sd, &master_fds); // Rimuove il FD dal set master

    // Trova lo slot del client e lo sposta
    for (int i = 0; i < num_clients; ++i) {
        if (clients[i].fd == sd) {
            // Sposta l'ultimo client nella posizione corrente per riempire il buco
            clients[i] = clients[num_clients - 1];
            // Resetta l'ultimo slot, non strettamente necessario ma buona pratica
            clients[num_clients - 1].fd = 0;
            num_clients--;
            printf("Client FD %d disconnesso e rimosso dal server. Client attivi: %d\n", sd, num_clients);
            return;
        }
    }
}

/**
 * @brief Trova una partita tramite il suo ID.
 * @param game_id L'ID della partita.
 * @return Puntatore alla struttura Game o NULL se non trovata.
 */
Game* find_game_by_id(int game_id) {
    for (int i = 0; i < MAX_GAMES; ++i) {
        if (games[i].id == game_id) {
            return &games[i];
        }
    }
    return NULL;
}

/**
 * @brief Trova una partita a cui è associato un dato giocatore.
 * @param player_fd Il file descriptor del giocatore.
 * @return Puntatore alla struttura Game o NULL se non in partita.
 */
Game* find_game_by_player_fd(int player_fd) {
    Client *client = find_client_by_fd(player_fd);
    if (client && client->game_id != -1) {
        return find_game_by_id(client->game_id);
    }
    return NULL;
}

/**
 * @brief Trova un client tramite il suo file descriptor.
 * @param client_fd Il file descriptor del client.
 * @return Puntatore alla struttura Client o NULL se non trovata.
 */
Client* find_client_by_fd(int client_fd) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd == client_fd) {
            return &clients[i];
        }
    }
    return NULL;
}

/**
 * @brief Stampa la lista delle partite disponibili a un client.
 * @param client_fd Il file descriptor del client a cui inviare la lista.
 */
void print_game_list(int client_fd) {
    char buffer[BUFFER_SIZE * 2]; 
    // Intestazione con linee decorative e titoli di colonna
    int offset = snprintf(buffer, sizeof(buffer), 
                          "\n--- PARTITE DISPONIBILI ---\n"
                          "%-5s | %-30s\n"
                          "---------------------------------\n", 
                          "ID", "STATO");
    
    bool found_games = false;

    for (int i = 0; i < MAX_GAMES; ++i) {
        if (games[i].id != -1) { 
            found_games = true;
            char *state_str;

            switch (games[i].state) {
                case GAME_NEW:                state_str = "NUOVA (attesa proprietario)"; break;
                case GAME_WAITING_FOR_PLAYER: state_str = "IN ATTESA DI GIOCATORE"; break;
                case GAME_IN_PROGRESS:        state_str = "IN CORSO"; break;
                case GAME_ENDED:              state_str = "TERMINATA"; break;
                default:                      state_str = "SCONOSCIUTO"; break;
            }

            // %-5d allinea l'ID in uno spazio di 5 caratteri
            // %-30s allinea lo stato in uno spazio di 30 caratteri
            offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                               "%-5d | %-30s\n",
                               games[i].id, state_str);
        }
    }
    
    if (!found_games) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                           "Nessuna partita disponibile.\n"
                           "Usa 'create' per iniziarne una.\n");
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "---------------------------------\n");
    send_to_client(client_fd, buffer);
}

/**
 * @brief Invia un messaggio a tutti i client nella lobby (client non coinvolti nella partita).
 * @param game Puntatore alla struttura Game per cui inviare la notifica (opzionale, può essere NULL se non specifico a una partita)
 * @param message Il messaggio da inviare a tutti i client nella lobby.
 */
void broadcast_to_lobby(Game *game, const char *message) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd > 0 && clients[i].fd != game->owner_fd && clients[i].fd != game->opponent_fd) {
            send_to_client(clients[i].fd, message);
        }
    }
}

/**
 * @brief Invia lo stato attuale del tabellone e il turno ai giocatori della partita.
 * @param game Puntatore alla struttura Game.
 */
void send_game_state_to_players(Game *game) {
    char board_buffer[BUFFER_SIZE];
    print_board(&game->tris_game, board_buffer); // Assumendo print_board in tris_game.h

    char msg_owner[BUFFER_SIZE * 2]; // Dimensione aumentata per messaggio combinato
    char msg_opponent[BUFFER_SIZE * 2]; 
    
    // Inizializza i messaggi con lo stato del gioco
    snprintf(msg_owner, sizeof(msg_owner), "\nStato attuale della partita %d:\n%s", game->id, board_buffer);
    snprintf(msg_opponent, sizeof(msg_opponent), "\nStato attuale della partita %d:\n%s", game->id, board_buffer);
    
    // Aggiungi informazioni sul turno corrente
    if (game->state == GAME_IN_PROGRESS) {
        Client* owner_client = find_client_by_fd(game->owner_fd);
        Client* opponent_client = find_client_by_fd(game->opponent_fd);
        // Controlla se i client sono validi
        if (game->tris_game.turn == 0) { // Turno di X (proprietario per la prima mossa)
            strcat(msg_owner, "È il tuo turno (X).\n");
            strcat(msg_opponent, "È il turno del tuo avversario (X).\n");
            if (owner_client) owner_client->is_current_turn = true;
            if (opponent_client) opponent_client->is_current_turn = false;
        // Imposta il turno del proprietario come attivo
        } else { // Turno di O (avversario)
            strcat(msg_owner, "È il turno del tuo avversario (O).\n");
            strcat(msg_opponent, "È il tuo turno (O).\n");
            if (owner_client) owner_client->is_current_turn = false;
            if (opponent_client) opponent_client->is_current_turn = true;
        }
    }
    // Invia i messaggi ai giocatori della partita
    send_to_client(game->owner_fd, msg_owner);
    if (game->opponent_fd != -1) {
        send_to_client(game->opponent_fd, msg_opponent);
    }
}

// --- Implementazioni delle Funzioni di Gestione Comandi ---

/**
 * @brief Gestisce il comando "create".
 * @param client_fd Il file descriptor del client.
 * @param current_client La struttura Client per il client corrente.
 */
void handle_create_command(int client_fd, Client *current_client) {
    // Controlla se il client è già in una partita
    if (current_client->game_id != -1) {
        send_to_client(client_fd, "Sei già in una partita. Lasciala prima di crearne una nuova.\n");
        return;
    }
    // Controlla se il numero massimo di partite è stato raggiunto
    if (num_games >= MAX_GAMES) {
        send_to_client(client_fd, "Massimo numero di partite raggiunto. Riprova più tardi.\n");
        return;
    }
    // Trova uno slot libero per una nuova partita
    int game_idx = -1;
    for (int j = 0; j < MAX_GAMES; ++j) {
        if (games[j].id == -1) { // Trova uno slot di gioco libero
            game_idx = j;
            break;
        }
    }
    // Se trovato uno slot libero, inizializza la nuova partita
    if (game_idx != -1) {
        Game *new_game = &games[game_idx];
        new_game->id = next_game_id++;
        new_game->owner_fd = client_fd;
        new_game->opponent_fd = -1;
        new_game->state = GAME_WAITING_FOR_PLAYER;
        new_game->last_result = IN_PROGRESS;

        init_game(&new_game->tris_game); // Inizializza la logica di gioco del tris
        
        // Inizializza il tabellone di gioco
        current_client->game_id = new_game->id;
        current_client->status = PLAYER_IN_GAME;
        current_client->player_symbol = X;
        current_client->is_current_turn = false; // Il turno sarà assegnato all'inizio del gioco
        current_client->wants_rematch = false;
        
        // Aggiungi il nuovo client alla partita
        num_games++;
        char msg[BUFFER_SIZE];
        snprintf(msg, sizeof(msg), "Partita creata con successo! ID: %d. Sei il giocatore X. In attesa di un avversario...\n", new_game->id);
        send_to_client(client_fd, msg);
        printf("Partita %d creata da FD %d. Stato: WAIT_FOR_PLAYER\n", new_game->id, client_fd);

        snprintf(msg, sizeof(msg), "Nuova partita disponibile (ID: %d) in attesa di un giocatore.\n", new_game->id);
        broadcast_to_lobby(new_game, msg);
    // Non è stato possibile trovare uno slot libero
    } else {
        send_to_client(client_fd, "Impossibile creare una nuova partita in questo momento.\n");
    }
}

/**
 * @brief Gestisce il comando "join".
 * @param client_fd Il file descriptor del client.
 * @param current_client La struttura Client per il client corrente.
 * @param buffer La stringa del comando ricevuto.
 */
void handle_join_command(int client_fd, Client *current_client, const char *buffer) {
    // Controlla se il client è già in una partita
    if (current_client->game_id != -1) {
        send_to_client(client_fd, "Sei già in una partita. Lasciala prima di unirti a una nuova.\n");
        return;
    }

    int game_id_to_join = atoi(buffer + 5); // Estrae l'ID dopo "join "
    Game *game = find_game_by_id(game_id_to_join);
    
    // Controlla se la partita esiste e se è in uno stato valido per unirsi
    if (!game || game->id == -1) {
        send_to_client(client_fd, "Partita non trovata o ID non valido.\n");
        // La partita è già in corso o ha un avversario
    } else if (game->state == GAME_IN_PROGRESS || game->state == GAME_ENDED || game->opponent_fd != -1) { 
        send_to_client(client_fd, "La partita è già in corso, terminata o ha già un avversario.\n");
        // Il client è il proprietario della partita
    } else if (game->owner_fd == client_fd) { 
        send_to_client(client_fd, "Non puoi unirti alla tua stessa partita. Sei già il proprietario.\n");
        // La partita è in attesa di un avversario
    } else { 
        // Il client richiede di unirsi
        game->opponent_fd = client_fd; // Imposta il file descriptor del client come avversario
        current_client->game_id = game->id; // Associa il client alla partita
        current_client->status = PLAYER_WAITING_ACCEPT; // Stato in attesa di accettazione
        current_client->player_symbol = O; // L'avversario è sempre O
        current_client->is_current_turn = false; // Non è il turno del nuovo giocatore
        current_client->wants_rematch = false; // Resetta la richiesta di rivincita
        
        // Invia un messaggio di conferma al client
        send_to_client(client_fd, "Richiesta inviata. In attesa di accettazione dal proprietario della partita...\n");
        char msg_owner[BUFFER_SIZE];
        snprintf(msg_owner, sizeof(msg_owner), "Un giocatore vuole unirsi alla tua partita %d. Digita 'accept' o 'reject'.\n", game->id);
        send_to_client(game->owner_fd, msg_owner);
        printf("FD %d ha richiesto di unirsi alla partita %d.\n", client_fd, game->id);
    }
}

/**
 * @brief Gestisce il comando "accept".
 * @param client_fd Il file descriptor del client.
 * @param current_client La struttura Client per il client corrente.
 */
void handle_accept_command(int client_fd, Client *current_client) {
    Game *game = find_game_by_player_fd(client_fd); // Trova la partita a cui il client appartiene
    // Controlla se il client è il proprietario della partita e se è in attesa di un avversario
    if (!game || game->owner_fd != client_fd) {
        send_to_client(client_fd, "Questo comando è solo per i proprietari di partita in attesa di un avversario.\n");
        return;
    }
    // Controlla se la partita è in attesa di un avversario
    if (game->state != GAME_WAITING_FOR_PLAYER || game->opponent_fd == -1) {
        send_to_client(client_fd, "Nessun giocatore in attesa di accettazione o la partita non è in stato di attesa.\n");
        return;
    }

    game->state = GAME_IN_PROGRESS; // Imposta lo stato della partita come in corso
    Client *opponent_client = find_client_by_fd(game->opponent_fd); // Trova il client avversario
    // Controlla se l'avversario è valido
    if (opponent_client) {
        opponent_client->status = PLAYER_IN_GAME;
        current_client->is_current_turn = true; // X (Proprietario) inizia
        opponent_client->is_current_turn = false;
    }
    // Imposta lo stato del gioco per entrambi i giocatori
    send_to_client(client_fd, "Hai accettato il giocatore. La partita è iniziata!\n");
    // Invia un messaggio all'avversario
    if (game->opponent_fd != -1) { // Invia all'avversario solo se valido
        send_to_client(game->opponent_fd, "La tua richiesta è stata accettata. La partita è iniziata!\n");
    }

    send_game_state_to_players(game); // Invia lo stato iniziale del gioco

    printf("Partita %d: Il proprietario (FD %d) ha accettato FD %d. Stato: IN_PROGRESS.\n", game->id, client_fd, game->opponent_fd);
}

/**
 * @brief Gestisce il comando "reject".
 * @param client_fd Il file descriptor del client.
 * @param current_client La struttura Client per il client corrente.
 */
void handle_reject_command(int client_fd, Client *current_client) {
    Game *game = find_game_by_player_fd(client_fd);
    // Controlla se il client è il proprietario della partita e se è in attesa di un avversario
    if (!game || game->owner_fd != client_fd) {
        send_to_client(client_fd, "Questo comando è solo per i proprietari di partita in attesa di un avversario.\n");
        return;
    }
    // Controlla se la partita è in attesa di un avversario
    if (game->state != GAME_WAITING_FOR_PLAYER || game->opponent_fd == -1) {
        send_to_client(client_fd, "Nessun giocatore in attesa di rifiuto o la partita non è in stato di attesa.\n");
        return;
    }

    Client *opponent_client = find_client_by_fd(game->opponent_fd); // Trova il client avversario
    // Controlla se l'avversario è valido
    if (opponent_client) {
        send_to_client(opponent_client->fd, "La tua richiesta di unirti alla partita è stata rifiutata.\n");
        opponent_client->game_id = -1; // Resetta lo stato del client avversario
        opponent_client->status = PLAYER_CONNECTED; // Resetta lo stato del client avversario
        opponent_client->player_symbol = EMPTY; // Resetta il simbolo del client avversario
        opponent_client->is_current_turn = false; // Resetta il turno del client avversario
        opponent_client->wants_rematch = false; // Resetta la richiesta di rivincita dell'avversario
    }
    game->opponent_fd = -1; // Rimuovi l'avversario dallo slot del gioco
    send_to_client(client_fd, "Hai rifiutato il giocatore. La tua partita è di nuovo in attesa di un avversario.\n");
    printf("Partita %d: Il proprietario (FD %d) ha rifiutato FD %d. Stato: WAIT_FOR_PLAYER.\n", game->id, client_fd, opponent_client ? opponent_client->fd : -1);
    // Notifica che una partita è tornata disponibile
    char msg_spectators[BUFFER_SIZE];
    snprintf(msg_spectators, sizeof(msg_spectators), "La partita ID %d è tornata disponibile in attesa di un giocatore.\n", game->id);
    broadcast_to_lobby(game, msg_spectators);
}

/**
 * @brief Gestisce il comando "leave".
 * @param client_fd Il file descriptor del client.
 * @param current_client La struttura Client per il client corrente.
 */
void handle_leave_command(int client_fd, Client *current_client) {
    // Controlla se il client è in una partita
    if (current_client->game_id == -1) {
        send_to_client(client_fd, "Non sei in una partita da lasciare.\n");
        return;
    }

    Game* game = find_game_by_id(current_client->game_id); // Trova la partita a cui il client appartiene
    // Controlla se la partita esiste
    if (!game) {
        // Dovrebbe essere gestito da remove_client_from_game, ma precauzione
        send_to_client(client_fd, "Errore interno: partita non trovata. Riprova o disconnetti.\n");
        current_client->game_id = -1; // Resetta stato del client
        current_client->status = PLAYER_CONNECTED; // Resetta lo stato del client
        current_client->player_symbol = EMPTY; // Resetta il simbolo del client
        current_client->is_current_turn = false; // Resetta il turno del client
        current_client->wants_rematch = false; // Resetta la richiesta di rivincita
        return;
    }
    
    send_to_client(client_fd, "Hai lasciato la partita.\n"); // Informa il client che ha lasciato la partita
    printf("Client FD %d ha lasciato la partita %d.\n", client_fd, game->id);
    
    // Rimuovi il client dalla partita
    remove_client_from_game(client_fd);
}

/**
 * @brief Gestisce il comando "move", validando la mossa, aggiornando lo stato del gioco e notificando i giocatori,
 * e gestisce le statistiche nei casi di vittoria, sconfitta o pareggio.
 * @param sd Il file descriptor del client che ha inviato il comando.
 * @param buffer Il buffer contenente il comando (es. "move 0 0").
 */
void handle_move_command(int sd, const char* buffer) {
    Client* current_client = find_client_by_fd(sd); 
    
    // 1. Validazione: il client è in partita?
    if (!current_client || current_client->status != PLAYER_IN_GAME) {
        send_to_client(sd, "Non sei in una partita. Digita 'join <game_id>' o 'create'.\n");
        return;
    }

    Game* game = find_game_by_id(current_client->game_id); 
    
    // 2. Validazione: la partita è attiva?
    if (!game || game->state != GAME_IN_PROGRESS) {
        if (game && game->state == GAME_ENDED) {
            send_to_client(sd, "La partita è terminata. Digita 'rematch' per rigiocare o 'leave' per uscire.\n");
        } else {
            send_to_client(sd, "La partita non è in corso o non è valida.\n");
        }
        return;
    }

    // 3. Validazione: è il turno del client?
    if (!current_client->is_current_turn) {
        send_to_client(sd, "Non è il tuo turno.\n");
        return;
    }

    // 4. Parsing della mossa
    int row, col;
    if (sscanf(buffer, "move %d %d", &row, &col) != 2) {
        send_to_client(sd, "Formato 'move' non valido. Usa: move <riga> <colonna> (es. move 0 0).\n");
        return;
    }

    // 5. Esecuzione mossa
    if (make_move(&game->tris_game, row, col) == 0) {
        GameResult result = check_winner(&game->tris_game);
        Client* owner_client = find_client_by_fd(game->owner_fd);
        Client* opponent_client = (game->opponent_fd != -1) ? find_client_by_fd(game->opponent_fd) : NULL;

        char board_str[BUFFER_SIZE];
        print_board(&game->tris_game, board_str);

        // --- CASO VITTORIA ---
        if (result == WIN) { 
            Client* winner_client = current_client;
            Client* loser_client = (winner_client->fd == game->owner_fd) ? opponent_client : owner_client;

            // Aggiorna statistiche classifica
            winner_client->wins++;
            if (loser_client) {
                loser_client->losses++;
            }

            // Notifica Vincitore
            send_to_client(winner_client->fd, "\nLa partita è terminata!\n");
            send_to_client(winner_client->fd, board_str);
            send_to_client(winner_client->fd, "HAI VINTO!\n");

            // Notifica Perdente
            if (loser_client) { 
                send_to_client(loser_client->fd, "\nLa partita è terminata!\n");
                send_to_client(loser_client->fd, board_str);
                send_to_client(loser_client->fd, "HAI PERSO.\n");
                send_to_client(loser_client->fd, "Sei stato rimosso dalla partita. Digita 'list' o 'create'.\n");
            }
            
            // Logica: il vincitore resta e aspetta un nuovo sfidante
            game->owner_fd = winner_client->fd;
            game->opponent_fd = -1;
            game->state = GAME_WAITING_FOR_PLAYER;
            init_game(&game->tris_game);
            game->last_result = IN_PROGRESS;

            winner_client->status = PLAYER_IN_GAME;
            winner_client->player_symbol = X; // Il vincitore ricomincia come X
            winner_client->is_current_turn = false; // Attende l'accettazione di un nuovo avversario
            
            if (loser_client) {
                remove_client_from_game(loser_client->fd);
            }
            
            char winner_prompt[BUFFER_SIZE]; 
            snprintf(winner_prompt, sizeof(winner_prompt), "Ora sei il proprietario della partita %d. In attesa di uno sfidante...\n", game->id);
            send_to_client(winner_client->fd, winner_prompt);

        // --- CASO PAREGGIO ---
        } else if (result == DRAW) { 
            // Aggiorna statistiche classifica per entrambi
            if (owner_client) owner_client->draws++;
            if (opponent_client) opponent_client->draws++;

            char msg_draw[BUFFER_SIZE * 2];
            snprintf(msg_draw, sizeof(msg_draw), "\nPAREGGIO!\n%s", board_str);

            send_to_client(game->owner_fd, msg_draw);
            send_to_client(game->owner_fd, "Digita 'rematch' per rigiocare o 'leave' per uscire.\n");
            
            if (game->opponent_fd != -1) { 
                send_to_client(game->opponent_fd, msg_draw);
                send_to_client(game->opponent_fd, "Digita 'rematch' per rigiocare o 'leave' per uscire.\n");
            }

            game->state = GAME_ENDED;
            game->last_result = DRAW;
            if(owner_client) owner_client->is_current_turn = false;
            if(opponent_client) opponent_client->is_current_turn = false;

        // --- CASO PARTITA IN CORSO ---
        } else { 
            send_game_state_to_players(game);
        }
    } else { 
        send_to_client(sd, "Mossa non valida (cella occupata o fuori limite).\n");
    }
}

/**
 * @brief Gestisce il comando "rematch".
 * @param sd Il file descriptor del client che ha inviato il comando.
 */
void handle_rematch_command(int sd) {
    Client* current_client = find_client_by_fd(sd); // Trova il client corrente
    // Controlla se il client è in una partita
    if (!current_client || current_client->game_id == -1) { 
        send_to_client(sd, "Non sei in una partita terminata per richiedere una rivincita.\n");
        return;
    }

    Game* game = find_game_by_id(current_client->game_id); // Trova la partita a cui il client appartiene
    // Controlla se la partita esiste e se è in uno stato di pareggio
    if (!game || game->state != GAME_ENDED || game->last_result != DRAW) {
        send_to_client(sd, "Questa partita non è in stato di pareggio per una rivincita.\n");
        return;
    }

    // Registra la richiesta di rivincita del client
    current_client->wants_rematch = true; // Indica che il client vuole una rivincita
    send_to_client(sd, "Richiesta di rivincita inviata. In attesa dell'altro giocatore...\n");
    printf("Client FD %d ha richiesto rivincita per partita %d.\n", sd, game->id);

    // Controlla se entrambi i giocatori vogliono la rivincita
    Client* owner_client = find_client_by_fd(game->owner_fd);
    Client* opponent_client = find_client_by_fd(game->opponent_fd);

    if (owner_client && opponent_client && owner_client->wants_rematch && opponent_client->wants_rematch) {
        // Entrambi i giocatori vogliono una rivincita!
        init_game(&game->tris_game); // Reset della board
        game->state = GAME_IN_PROGRESS; // Ritorna in corso
        game->last_result = IN_PROGRESS; // Resetta risultato

        // Resetta i turni e i simboli dei giocatori
        if (game->tris_game.turn == 0) { // Se X (proprietario) aveva iniziato
            game->tris_game.turn = 1; // O (avversario) inizia ora
            owner_client->is_current_turn = false; // Resetta il turno del proprietario
            opponent_client->is_current_turn = true; // O (avversario) inizia ora
        } else { // Se O (avversario) aveva iniziato (o se fosse stato reimpostato a 0 dopo vittoria)
            game->tris_game.turn = 0; // X (proprietario) inizia ora
            owner_client->is_current_turn = true; // X (proprietario) inizia ora
            opponent_client->is_current_turn = false; // Resetta il turno dell'avversario
        }
        
        owner_client->wants_rematch = false; // Resetta lo stato di richiesta
        opponent_client->wants_rematch = false; // Resetta lo stato di richiesta

        send_to_client(owner_client->fd, "Entrambi avete richiesto una rivincita! La nuova partita inizia.\n");
        send_to_client(opponent_client->fd, "Entrambi avete richiesto una rivincita! La nuova partita inizia.\n");
        printf("Partita %d: Rivincita accettata. Nuova partita iniziata.\n", game->id);

        send_game_state_to_players(game); // Invia il nuovo stato del tabellone e il turno
        // Solo uno dei giocatori ha richiesto una rivincita
    } else { 
        // Notifica l'altro giocatore della richiesta di rivincita
        Client* other_player = (current_client->fd == owner_client->fd) ? opponent_client : owner_client;
        if (other_player && other_player->game_id == game->id) { // Assicurati che l'altro giocatore sia ancora nella stessa partita
            send_to_client(other_player->fd, "L'altro giocatore ha richiesto una rivincita. Digita 'rematch' per accettare o 'leave' per uscire.\n");
        }
    }
}

/**
 * @brief Gestisce i dati in ingresso da un client.
 * @param sd Il file descriptor del client.
 * @param buffer Il buffer contenente i dati ricevuti.
 * @param valread Il numero di byte letti.
 */
void handle_client_data(int sd, char *buffer, int valread) {
    buffer[valread] = '\0';
    // Rimuovi il carattere newline finale se presente
    buffer[strcspn(buffer, "\n")] = 0;

    printf("Ricevuto da FD %d: '%s'\n", sd, buffer);

    Client *current_client = find_client_by_fd(sd);
    if (!current_client) {
        send_to_client(sd, "Errore interno del server, client non trovato.\n");
        return;
    }

    if (strcmp(buffer, "list") == 0) { // Comando per elencare le partite disponibili
        print_game_list(sd);

    
    } else if (strcmp(buffer, "create") == 0) { // Comando per creare una nuova partita
        handle_create_command(sd, current_client);
    } else if (strncmp(buffer, "join ", 5) == 0) { // Comando per unirsi a una partita
        handle_join_command(sd, current_client, buffer);
    } else if (strcmp(buffer, "accept") == 0) { // Comando per accettare una richiesta di unione a una partita
        handle_accept_command(sd, current_client);
    } else if (strcmp(buffer, "reject") == 0) { // Comando per rifiutare una richiesta di unione a una partita
        handle_reject_command(sd, current_client);
    } else if (strcmp(buffer, "leave") == 0) { // Comando per lasciare una partita
        handle_leave_command(sd, current_client);
    } else if (strncmp(buffer, "move ", 5) == 0) { // Comando per effettuare una mossa
        handle_move_command(sd, buffer); // Chiamata corretta con 2 argomenti
    } else if (strcmp(buffer, "rematch") == 0) { // Comando per richiedere una rivincita
        handle_rematch_command(sd); // Chiamata corretta con 1 argomento
    } else if (strcmp(buffer, "statistics") == 0) { // Comando per visualizzare le statistiche dei giocatori
        handle_statistics_command(sd);
    } else if (strcmp(buffer, "quit") == 0) { // Comando per uscire dal server
        send_to_client(sd, "Arrivederci!\n");
        remove_client(sd); // Rimuovi il client completamente
    } else {
        send_to_client(sd, "Digita <create> per creare una stanza, <join> per unirti, <accept> per accettare una richiesta, <reject> per rifiutare una richiesta, <leave> per disconetterti dalla partita, <move> <riga> <colonna> per fare la tua mossa, <statistics> per visualizzare le statistiche, <quit> per uscire dal gioco.\n");
    }
}

/**
 * @brief Invia la statistica dettagliata al client.
 * Mostra Vittorie, Pareggi e Sconfitte per ogni giocatore connesso.
 */
void handle_statistics_command(int client_fd) {
    char buffer[BUFFER_SIZE];
    
    // Intestazione della tabella con formattazione fissa
    int offset = snprintf(buffer, sizeof(buffer), 
        "\n--- CLASSIFICA GENERALE ---\n"
        "%-15s | %-10s | %-10s | %-10s\n"
        "------------------------------------------------------\n", 
        "GIOCATORE", "Vittoria", "Pareggio", "Sconfitta");
    
    // Scansione dei client per riempire le righe
    for (int i = 0; i < MAX_CLIENTS; i++) {
        // Mostriamo solo gli slot occupati da client attivi
        if (clients[i].fd > 0) {
            // Controlla che non ci sia overflow del buffer durante la scrittura
            int remaining = sizeof(buffer) - offset;
            if (remaining > 0) {
                offset += snprintf(buffer + offset, remaining,
                                   "%-15s | %-10d | %-10d | %-10d\n", 
                                   clients[i].username, 
                                   clients[i].wins, 
                                   clients[i].draws, 
                                   clients[i].losses);
            }
        }
    }
    
    // Chiusura della tabella
    if (sizeof(buffer) - offset > 0) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                           "------------------------------------------------------\n");
    }

    send_to_client(client_fd, buffer);
}

// --- Funzione Main del Server ---

int main(int argc, char *argv[]) {
    int master_socket, new_socket, activity, i, valread, sd; // File descriptor del socket master
    struct sockaddr_in address; // Struttura per l'indirizzo del server
    int addrlen; // Lunghezza dell'indirizzo
    char buffer[BUFFER_SIZE]; // Buffer per i dati ricevuti

    // Inizializza tutti i client e giochi a 0 / -1
    for (i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = 0;
        clients[i].game_id = -1; // Nessuna partita
    }
    // Inizializza tutti i giochi a -1 (slot libero)
    for (i = 0; i < MAX_GAMES; i++) {
        games[i].id = -1; // Slot di gioco libero
    }

    // Crea il socket master
    if ((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Imposta le opzioni del socket (riuso dell'indirizzo)
    int opt = 1;
    if (setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Prepara la struttura dell'indirizzo
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    addrlen = sizeof(address);

    // Associa il socket all'indirizzo e alla porta specificati
    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    printf("Server in ascolto sulla porta %d\n", PORT); 

    // Metti il socket in modalità ascolto (max 50 connessioni in coda)
    if (listen(master_socket, 50) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // Inizializza il set di file descriptor master
    FD_ZERO(&master_fds);
    FD_SET(master_socket, &master_fds);
    max_sd = master_socket;

    printf("In attesa di connessioni...\n");

    // Ciclo principale del server
    while (true) {
        fd_set read_fds = master_fds; // Copia il set master per select()

        // Aspetta indefinitamente un'attività su uno dei socket
        activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);

        // Controlla se c'è un errore nella select
        if ((activity < 0) && (errno != EINTR)) {
            printf("select error\n");
        }

        // Se c'è attività sul socket master, è una nuova connessione
        if (FD_ISSET(master_socket, &read_fds)) {
            if ((new_socket = accept(master_socket, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }

            printf("Nuova connessione, socket fd è %d, ip è : %s, porta : %d\n", new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

            // Aggiungi il nuovo socket al set di master_fds
            FD_SET(new_socket, &master_fds); // Aggiungi il nuovo socket al set di file descriptor
            // Aggiorna il massimo file descriptor
            if (new_socket > max_sd) {
                max_sd = new_socket;
            }

            initialize_client(new_socket); // Inizializza la struttura client
        }

        // Altrimenti, è attività su un socket client esistente
        for (i = 0; i < MAX_CLIENTS; i++) {
            sd = clients[i].fd;
            // Controlla se il socket è valido e ha attività
            if (sd > 0 && FD_ISSET(sd, &read_fds)) { // Se il socket è valido e ha attività
                // Leggi i dati dal client
                if ((valread = read(sd, buffer, BUFFER_SIZE)) == 0) {
                    // Client disconnesso
                    printf("Host disconnesso, fd %d\n", sd);
                    remove_client(sd); // Rimuovi il client
                } else {
                    // C'è del dato dal client
                    handle_client_data(sd, buffer, valread);
                }
            }
        }
    }
    
    return 0;
}