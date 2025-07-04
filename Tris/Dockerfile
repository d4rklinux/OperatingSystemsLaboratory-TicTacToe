# Usa l'immagine base di Ubuntu 22.04
FROM ubuntu:22.04

# Aggiorna l'elenco dei pacchetti e installa i pacchetti necessari per compilazione e debugging
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    make \
    net-tools \
    iputils-ping \
    vim && \
    rm -rf /var/lib/apt/lists/*

# Crea le directory per il server e il client
RUN mkdir -p /app/server /app/client

# Copia i file sorgente del server nella directory corrispondente
COPY server.c /app/server/
COPY tris_game.c /app/server/
COPY tris_game.h /app/server/

# Copia il file sorgente del client nella directory corrispondente
COPY client.c /app/client/

# Imposta la directory di lavoro al server
WORKDIR /app/server

# Compila il server, linkando il file tris_game.c e la libreria pthread (per il multithreading)
RUN gcc server.c tris_game.c -o server -lpthread -std=c99

# Imposta la directory di lavoro al client
WORKDIR /app/client

# Compila il client
RUN gcc client.c -o client

# Comando di default all'avvio del container: esegue il server
CMD ["/app/server/server"]
