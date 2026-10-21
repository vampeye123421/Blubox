# Blubox Client and Launcher

Base C++20 per Blubox. `blubox_launcher` e' il bootstrapper leggero con UI Raylib; `blubox_client` contiene il gioco con Raylib e Jolt Physics; `blubox_server` e' il server dedicato headless basato su ENet.

## Prerequisiti

- CMake 3.24 o superiore
- Un compilatore C++20 (MSVC, Clang o GCC)
- Git e accesso a Internet al primo configure: CMake scarica Raylib 5.5, Jolt Physics 5.3.0, ENet e nlohmann/json
- OpenSSL development package sui sistemi non Windows (Windows usa CNG/BCrypt incluso nel sistema)

## Build

Da PowerShell, nella cartella del progetto:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Con Visual Studio vengono creati `build/Release/blubox_launcher.exe` e `build/Release/blubox_client.exe`. Con un generatore single-configuration si trovano direttamente in `build/`. CMake colloca il launcher, il client payload e `version.json` nella stessa cartella di output.

## Avvio e aggiornamenti

```powershell
./build/Release/blubox_launcher.exe --mode=guest
./build/Release/blubox_launcher.exe --mode=guest --fullscreen
./build/Release/blubox_launcher.exe "blubox://place/123"
```

Senza argomenti parte la scena locale guest. Il launcher controlla il feed HTTPS in `version.json` pubblicato nel repository Blubox, scarica l'eseguibile client indicato dal manifest e verifica lo SHA-256 prima di installarlo in `%LOCALAPPDATA%\Blubox\versions\<version>\blubox_client.exe`. Usa `blubox_launcher.exe --studio` per installare o aggiornare Blubox Studio; il launcher salva anche un collegamento nel menu Start e registra il protocollo `blubox-studio://` per il login web.

## Registrare `blubox://` su Windows

Esegui una volta il launcher:

```powershell
.\build\Release\blubox_launcher.exe --register-protocol
```

Con MinGW o un generatore single-configuration usa `./build/blubox_launcher.exe --register-protocol`. La registrazione viene scritta in `HKEY_CURRENT_USER\Software\Classes\blubox`, non richiede privilegi amministrativi e punta al launcher. Per rimuoverla:

```powershell
.\build\Release\blubox_launcher.exe --unregister-protocol
```

Una volta registrato, Windows avvia il launcher passando l'URL; il launcher inoltra al client gli argomenti ricevuti, incluso `blubox://join?token=...&worldId=123`. Il client estrae il token e l'ID mondo in `ClientSession`, mantenuta solo in memoria. Il token viene cancellato dalle copie URI/query dopo l'estrazione e sovrascritto quando la sessione viene distrutta; non viene scritto su file né incluso nei log. Il client conserva i dati di routing non sensibili in `ProtocolLaunchData`. I valori percent-encoded vengono decodificati; i parametri duplicati o con encoding non valido vengono rifiutati. Le richieste `join` senza token o senza world ID generano un avviso.

Per un URI `join` valido, il client apre una connessione ENet UDP asincrona verso `blubox-node1.duckdns.org:7777` e invia su canale affidabile un handshake versione 2 con il token e il world ID. Il loop usa `enet_host_service(..., 0)` senza bloccare il rendering. Dopo l'autenticazione invia la posizione del giocatore a 20 Hz; il server trasmette gli snapshot del mondo a 60 Hz. ENet non cifra il traffico.

## Controlli

- `W`, `A`, `S`, `D` (o frecce): muovi il giocatore
- `SPACE`: salta quando il personaggio e' a terra
- Mouse: ruota la visuale in terza persona
- `TAB`: cattura o libera il mouse (`clic sinistro` per ricatturarlo)
- `F11`: attiva o disattiva il fullscreen
- `ESC`: esci

La fisica usa un accumulatore a passo fisso di 120 Hz, con un limite al recupero dopo pause o finestre trascinate. Il framerate grafico ha un obiettivo di 180 FPS e la finestra e' ridimensionabile.

## Server dedicato

Il server dedicato e il suo updater sono mantenuti separatamente nel repository
privato `vampeye123421/blubox-server`. Il client usa il protocollo ENet versione
2 e si connette al nodo configurato dal flusso di join del sito.
