# Gioco del Tris (Tic-Tac-Toe)

Questo progetto implementa un gioco del Tris (Tic-Tac-Toe) multi-client utilizzando socket C, permettendo a più giocatori di connettersi a un server centrale, creare partite, unirsi a quelle esistenti e giocare l'uno contro l'altro. L'intera configurazione è containerizzata tramite Docker, rendendo facile la compilazione e l'esecuzione.

## Funzionalità

* **Multigiocatore**: Supporta la connessione di più client a un singolo server.
* **Gestione delle Partite**: I client possono creare nuove partite e unirsi a quelle disponibili.
* **Gameplay Interattivo**: I giocatori possono fare mosse specificando riga e colonna.
* **Stati del Gioco**: Gestisce vari stati del gioco, inclusi l'attesa di giocatori, la partita in corso, la vittoria e il pareggio.
* **Funzionalità di Rivincita**: I giocatori possono richiedere una rivincita dopo un pareggio.
* **Comunicazione Client-Server**: Utilizza socket TCP per una comunicazione affidabile.
* **Dockerizzato**: Facile configurazione e deployment tramite Docker e Docker Compose.

---

## Tecnologie Utilizzate

* **Linguaggio C**: Logica principale sia per il server che per il client.
* **Socket**: Per la comunicazione di rete tra client e server.
* **`select()`**: Per la gestione concorrente di più connessioni client sul server.
* **Docker**: Per la containerizzazione dell'applicazione.
* **Docker Compose**: Per l'orchetrazione dei container del server e del client.

---

## Come Compilare ed Eseguire

Il progetto è progettato per essere compilato ed eseguito utilizzando Docker Compose, che semplifica la configurazione di entrambi i componenti server e client.

### Prerequisiti

* Docker
* Docker Compose

### Passaggi

1.  **Assicurati che tutti i file forniti siano nella stessa directory:**

    ```
    .
    ├── client.c
    ├── docker-compose.yml
    ├── Dockerfile
    ├── server.c
    ├── tris_game.c
    └── tris_game.h
    ```

2.  **Avvia l'Intera Applicazione (Server e un Client Iniziale)**:
    Naviga nella directory contenente `docker-compose.yml` ed esegui:

    ```bash
    docker-compose build
    ```
    Questo comando:
    * Costruirà un'immagine Docker basata sul `Dockerfile`.
    * Creerà e avvierà il container `server`, esponendo la porta 8080.
    * Creerà e avvierà un container `client` di base chiamato `tris_client_base`. Questo client sarà connesso al server.

    Dovresti vedere l'output sia del server che del client iniziale nel tuo terminale. Il server sarà in ascolto sulla porta 8080. Il client tenterà di connettersi a `SERVER_HOST: server` e `SERVER_PORT: 8080`, che sono variabili d'ambiente impostate in `docker-compose.yml` per puntare al servizio del server all'interno della rete Docker.

### Connessione di Client Aggiuntivi

Per simulare più giocatori, puoi avviare istanze client aggiuntive dal tuo terminale **mentre `docker compose` è ancora in esecuzione**:

```bash
docker-compose run --rm client 
