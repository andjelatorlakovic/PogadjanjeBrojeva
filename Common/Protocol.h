enum RequestType {
    CONNECT = 1,       // klijent se povezuje / registruje
    START_GAME = 2,    // zapoèni igru
    GUESS = 3,         // šalje pretpostavku
    DISCONNECT = 4,    // prekid veze

    INFO = 10,         // server info poruke
    RESULT = 11,       // odgovor: VECE / MANJE / POGODJENO
    WIN = 12,          // pobeda
    LOSE = 13          // poraz (sa tacnim brojem)
};
