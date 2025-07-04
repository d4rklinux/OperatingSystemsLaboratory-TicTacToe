#ifndef TRIS_GAME_H
#define TRIS_GAME_H

#define SIZE 3

// Stato di ogni cella del tabellone
typedef enum { EMPTY, X, O } Cell;

// Stato della partita
typedef enum { IN_PROGRESS, WIN, DRAW } GameResult;

// Struttura dati che rappresenta lo stato di una partita di tris
typedef struct {
    Cell board[SIZE][SIZE]; // Griglia di gioco
    int turn;               // 0 = turno X, 1 = turno O
} TrisGame;

// Inizializza la partita: svuota il tabellone e imposta il turno a X
void init_game(TrisGame *game);

// Esegue una mossa nella cella (row, col); restituisce 0 se valida, -1 altrimenti
int make_move(TrisGame *game, int row, int col);

// Verifica lo stato attuale della partita: vittoria, pareggio o in corso
GameResult check_winner(TrisGame *game);

// Scrive la rappresentazione testuale del tabellone nel buffer fornito
void print_board(TrisGame *game, char *buffer);

#endif // TRIS_GAME_H
