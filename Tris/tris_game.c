#include "tris_game.h"
#include <string.h>

// Inizializza una nuova partita svuotando il tabellone e impostando il turno iniziale al giocatore X (turno = 0).
void init_game(TrisGame *game) {
    memset(game, 0, sizeof(TrisGame));
}


// Tenta di effettuare una mossa sulla cella specificata. Restituisce 0 se la mossa è valida, -1 altrimenti.
int make_move(TrisGame *game, int row, int col) {
    if (row < 0 || row >= SIZE || col < 0 || col >= SIZE)
        return -1;
    if (game->board[row][col] != EMPTY)
        return -1;

    game->board[row][col] = (game->turn == 0) ? X : O;
    game->turn = 1 - game->turn;
    return 0;
}

// Verifica lo stato della partita: vittoria, pareggio o in corso. Restituisce WIN, DRAW o IN_PROGRESS.
GameResult check_winner(TrisGame *game) {
    Cell (*b)[SIZE] = game->board;

    // Controllo righe e colonne
    for (int i = 0; i < SIZE; ++i) {
        if (b[i][0] != EMPTY && b[i][0] == b[i][1] && b[i][1] == b[i][2])
            return WIN;
        if (b[0][i] != EMPTY && b[0][i] == b[1][i] && b[1][i] == b[2][i])
            return WIN;
    }

    // Controllo diagonali
    if (b[0][0] != EMPTY && b[0][0] == b[1][1] && b[1][1] == b[2][2])
        return WIN;
    if (b[0][2] != EMPTY && b[0][2] == b[1][1] && b[1][1] == b[2][0])
        return WIN;

    // Verifica celle vuote rimaste
    for (int i = 0; i < SIZE; ++i)
        for (int j = 0; j < SIZE; ++j)
            if (b[i][j] == EMPTY)
                return IN_PROGRESS;

    return DRAW;
}

// Stampa il tabellone di gioco nel buffer dato. I simboli usati sono: '.' = vuoto, 'X', 'O'
void print_board(TrisGame *game, char *buffer) {
    char *ptr = buffer;

    for (int i = 0; i < SIZE; ++i) {
        // Stampa una riga di celle con separatori verticali
        for (int j = 0; j < SIZE; ++j) {
            char c = (game->board[i][j] == EMPTY) ? '.' :
                     (game->board[i][j] == X) ? 'X' : 'O';
            *ptr++ = ' ';
            *ptr++ = c;
            *ptr++ = ' ';
            if (j < SIZE - 1) {
                *ptr++ = '|';  // separatore verticale
            }
        }
        *ptr++ = '\n';

        // Stampa la linea orizzontale tranne dopo l'ultima riga
        if (i < SIZE - 1) {
            for (int k = 0; k < SIZE; ++k) {
                *ptr++ = '-';
                *ptr++ = '-';
                *ptr++ = '-';
                if (k < SIZE - 1) {
                    *ptr++ = '+';
                }
            }
            *ptr++ = '\n';
        }
    }
    *ptr = '\0';
}