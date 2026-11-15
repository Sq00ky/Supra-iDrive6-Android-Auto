# Test6wb temporaneo dentro NBT-AAD

Implementazione temporanea su `MLiv79/BMW-Android-Auto`, ramo `feature/bmw-6wb`.
Sorgente dei test: `MLiv79/BMW-6WB-Test` al commit
`27c0291cb9dedfe7c5b1c1ac1d029b153e05b6e5`.
Non compilata, non eseguita in Codex e non verificata sulla vettura.
L'integrazione non dimostra la causa o la risoluzione dei SIGSEGV standalone.

## Architettura e gestione manuale

Il modulo prende in prestito **il medesimo HudTerminal** creato da KombiNav e
il riferimento al **medesimo g_most** già registrato dal daemon. Non costruisce
DsiClient, CDSIConnection, un altro most::Client, un altro Core o thread mittente
HUDTerminal; non chiama open/start/register del trasporto. Nessuna modifica
all'ABI vendor o al trasporto DSI/MOST.

I batch diagnostici passano da `HudTerminal::submit` e dalla sua coda esistente:
`pacedGraphics=true`, 15 ms, TX4 setter/vettori/testi, TX0 bitmap C05. I comandi
successivi non aspettano ACK grafici. L'owner diagnostico attende solo il dispatch
del batch tramite polling `waitIdle(0)`; per C04 attende i risultati obbligatori.
Il tag e l'indirizzo sono quelli della registrazione esistente. L'osservatore
aggiunto al wrapper HudTerminal registra tentativo, esito locale e risposte senza
sostituire il sender, alterare il risultato o effettuare I/O su disco dalle callback.
Le identità TX sono campionamenti locali del client, non una cattura sul bus.

Il motore è inattivo finché arriva `start`. A riposo non apre log, non interroga
risorse e non invia comandi MOST. Un thread gestisce il solo IPC locale e uno
esegue i lavori; un mutex ammette un solo lavoro diagnostico, compresi cleanup
ed eventuali richieste in attesa. Gli avvii concorrenti vengono rifiutati.

**La navigazione è interamente responsabilità dell'utilizzatore.** Verificare
manualmente che Android Auto e la navigazione OEM non stiano navigando e che
la precedente pulizia della navigazione sia terminata prima di `start`.
Non viene letto lo stato della navigazione, non viene sospeso il navigatore,
non vengono aggiunti blocchi o interruzioni automatiche all'avvio di una rotta.
La logica di produzione continua invariata. Se l'utilizzatore avvia navigazione
durante la prova, gli invii possono interferire sulla stessa coda/superficie:
la serializzazione riguarda solo i lavori diagnostici, non esclude il navigatore.

## Build solo sul PC Windows/QNX 6.5 ARM

La flag `TEST6WB_TEMP` è **disattivata di default**. Una build ordinaria non
include motore, IPC, osservatori o API temporanee e mantiene il target `nbt-aad`.
Per la build temporanea, dalla directory `milestone-7` nella propria shell SDK:

```text
make TEST6WB_TEMP=1 nbt-aad-test6wb-temp test6wbctl
```

Non eseguire questi comandi in Codex. Il nuovo eseguibile daemon è
`nbt-aad-test6wb-temp`; il client locale è `test6wbctl`. Tutti gli object della
variante temporanea hanno suffisso `.test6wb-temp.o` e dependency file separati:
non si mescolano classi con e senza gli osservatori condizionali. Il target normale
e il suo binario non vengono sovrascritti dal link temporaneo.
Per l'eventuale progetto CMake usare una directory di build distinta con
`-DTEST6WB_TEMP=ON`; l'opzione è OFF per default e cambia il nome dell'output
NBT-AAD nella sola variante abilitata. Il Makefile QNX resta il percorso principale.

Distribuire la variante temporanea mediante la propria procedura abituale,
conservando il binario di produzione per il ritorno successivo. Questa consegna
non installa o sostituisce automaticamente alcun file sulla vettura. Occorre
avviare una volta il daemon compilato con diagnostica, usando l'ambiente NBT-AAD
abituale con `NBT_CLUSTER=6wb` e `NBT_HUDTERM=on`. È una selezione del backend,
non un controllo della navigazione. Non cambiare configurazioni permanenti.
Dopo questo avvio, cambiare test non richiede alcun riavvio del daemon.
Non avviare due istanze del daemon né una copia standalone Test6wb.

Il client collega solo socket e runtime C++, senza sorgenti MOST/DSI. Il suo
RPATH include `/lib:/usr/lib:/net/hu-omap/fs/sda32/lib` per il runtime presente
sull'NBT; il link/RPATH del daemon di produzione non cambia. Se il nodo Qnet
non è disponibile o il runtime non è compatibile il client non può avviarsi:
non copiare librerie proprietarie per aggirare il problema.

## IPC locale e comandi SSH

Il controllo usa AF_UNIX/SOCK_STREAM, **nessuna porta o servizio di rete nuovo**.
Socket: `/dev/shmem/nbt-aad-test6wb-temp/control.sock`, in directory privata 0700,
con socket 0600 e lock POSIX `owner.lock`. Il client va eseguito con lo stesso
account (o autorizzazioni equivalenti) e sullo stesso nodo QNX del daemon.
Se SSH entra su un altro nodo, usare la shell del nodo già prevista nella propria
procedura NBT-AAD, non un secondo daemon. Il file socket su Qnet da solo non rende
remoto l'IPC Unix locale.

Esempi, assumendo di aver copiato il solo helper in `/dev/shmem/test6wbctl`:

```text
/dev/shmem/test6wbctl list
/dev/shmem/test6wbctl status
/dev/shmem/test6wbctl start test 001 --hold 20
/dev/shmem/test6wbctl status
/dev/shmem/test6wbctl stop
/dev/shmem/test6wbctl status
/dev/shmem/test6wbctl start range 001 006 --hold 20
/dev/shmem/test6wbctl start all --hold 20
```

`start` ritorna dopo accettazione, non dopo il completamento o la fotografia.
Può essere accettato durante l’inizializzazione: il worker aspetta il terminale
e la coda condivisa; nessun pattern viene inviato durante questa attesa.
`status` mostra disponibilità del backend, stato trasporto, busy, prova corrente,
fase, ultimo esito e percorso log. I controlli di readiness sono quelli del
trasporto e della configurazione LIVE 6WB, non controlli dello stato AA.
Se il backend non è disponibile il modulo risponde con errore, senza aprire
una connessione alternativa e senza abilitare automaticamente il LIVE.

`stop` richiede l'interruzione e lascia attivo l'IPC per altri comandi. La prova
ferma nuove sottomissioni, poi tenta cleanup. Non annulla o resetta il Core condiviso:
un batch già accodato può terminare. Durante C04 il drenaggio può durare fino
al limite di 42 s; per la sola grafica normalmente termina il dispatch locale.
Attendere `busy=0` prima di un altro `start`. Se il client segnala timeout della
risposta IPC, controllare `status` prima di ripetere il comando: potrebbe essere
stato accettato. Nessuna attesa di INVIO nella sequenza.

Opzioni: `--ready-timeout` secondi interi 1–600 (default 90), timeout complessivo
della fase risorse descritto sotto; `--hold` secondi interi 0–120 (default 20), oppure `--hold-ms` 0–120000;
`--settle-ms` 0–30000 (default 2000), `--pattern identity|bands` nei comandi
avanzati. I payload preparati sono sempre registrati; `--verbose` resta accettato
per compatibilità. `--live`, `--dry-run` e `--log` dello standalone non si applicano:
`start` è l'autorizzazione esplicita d'invio nella connessione già LIVE, il log è
separato e ha percorso fisso. Nessun parametro cambia la sessione MOST.

## Catalogo preservato e modalità avanzate

| ID | Handle | Prova |
|---|---|---|
| 001 | 0x18 | Icons A, bitmap rossa, 305×44 |
| 002 | 0x19 | Icons B, bitmap blu, 305×44 |
| 003 | 0x1F | Main A, identificazione vettoriale, 221×158 |
| 004 | 0x20 | Main B, identificazione vettoriale, 221×158 |
| 005 | 0x23 | Lanes A, identificazione vettoriale, 283×40 |
| 006 | 0x24 | Lanes B, identificazione vettoriale, 283×40 |
| 007 | 0x1A | Testo strada Latin-15 |
| 008 | 0x1B | Testo distanza Latin-15 |
| 009 | 0x1C | Testo unità Latin-15 |
| 010 | 0x35 | Barra, valore 128/255, 28×113 |

I builder e il catalogo sono quelli del commit sorgente richiesto: stessi glifi,
ID grafici Tnnn, dimensioni, griglia/marcatori, cap bitmap 4096, massimo 99 punti
per polilinea e 128 chiamate per batch. Il profilo applicativo impedisce bitmap
su testi, barra e contenitore. **0x17 non viene riempito:** solo query e visibilità.
Gli ID di catalogo sono fissi; Rnnn è l'occorrenza nel lavoro. Per le prove
parametriche il catalogo resta `test=000` e l'etichetta segue l'occorrenza.
Le limitazioni di font, colori tema e ingombri fisici restano da osservare.

```text
/dev/shmem/test6wbctl start isolated 0x1F --hold 20
/dev/shmem/test6wbctl start sequence graphics
/dev/shmem/test6wbctl start --pattern bands isolated 0x18
/dev/shmem/test6wbctl start compose 0x18 0x20 0x23
/dev/shmem/test6wbctl start ab 0x18
/dev/shmem/test6wbctl start bar 128
/dev/shmem/test6wbctl start text 0x1A "Via Roma"
/dev/shmem/test6wbctl start fill 0x1F theme
/dev/shmem/test6wbctl start border 0x19 white
/dev/shmem/test6wbctl start clear
/dev/shmem/test6wbctl start --script /dev/shmem/frame.txt --hold 20
```

Il client legge lo script e lo inoltra al daemon: massimo 8192 byte, 32 righe
operative di 255 byte, 320 prove espanse. Nessuna shell viene eseguita. Le opzioni
globali rimangono nella riga di avvio; le righe sono comandi diagnostici, con
spazi e commenti `#` come nello standalone. Il protocollo locale ha limiti di
16384 byte e 128 argomenti; non espone socket verso la rete.

Isolated/sequence/test/range/all/bar/text/fill/border puliscono prima del target;
compose pulisce una volta e aggiunge risorse di coppie differenti, rifiutando
duplicati e due buffer della stessa coppia. `ab` esegue target/partner/target senza
pulizia intermedia, senza supporre visibilità simultanea A/B.
`frame on/off` preserva i contenuti durante il lavoro; per osservarlo senza
cleanup fra off/on usare lo script `src/test6wb_temp/examples/frame.txt` copiato
manualmente sul target. Il cleanup finale resta attivo e non ripristina contenuti
OEM non salvati. Il vettoriale fill conserva la campitura piena con ID solo su
CLI/log; gli identificativi vettoriali sono in isolated/border e modalità fotografiche.

## Risorse, logging e interruzioni

La verifica usa la cache C04 del terminale già esistente. Il worker attende
prima `kReady` e poi la coda libera, anche se contiene una verifica C04 già in
corso. Non esiste più il limite iniziale di 5 s. Un unico budget, predefinito
a 90 s e configurabile con `--ready-timeout 120`, copre attesa del terminale,
drenaggio della coda preesistente, query mancanti e verifica delle otto dimensioni.
Il budget inizia entrando nella fase risorse, dopo la preparazione del log;
non comprende pause fotografiche o cleanup. Il polling ogni 10 ms avviene
soltanto nel worker: IPC, main e navigazione continuano normalmente.

Log e `status phase` distinguono `WAIT_TERMINAL` / `waiting terminal` da
`WAIT_C04` / `waiting C04/shared queue`; quest’ultima indica coda occupata,
che può contenere C04 oppure grafica: l’API condivisa non espone il tipo della
chiamata in corso. `RESOURCE_WAIT_DONE`, `RESOURCE_TIMEOUT` e
`RESOURCE_WAIT_STOP` registrano completamento, scadenza e stop.
`stop` interrompe entrambe le attese; se non è stato sottomesso alcun batch
diagnostico non viene toccata la coda di produzione. Se una C04 diagnostica
è già stata sottomessa, il cleanup può ancora attendere fino a 42 s per drenarla:
non annulla il lavoro condiviso. Il timeout termina la preparazione, senza
abilitare pattern; non allunga i deadline vendor/terminali delle singole C04.
`kFailed` interrompe subito il lavoro senza reset; dopo il primo aggancio al
terminale pronto, cambi di link/generazione interrompono il lavoro senza retry.
Non vengono letti stati della navigazione né aggiunte esclusioni del navigatore.

Dimensioni mancanti vengono richieste una sola volta, con lo stesso selettore e lo stesso deadline;
non viene ripetuta una C04 già in cache e non si resetta il terminale dopo un errore.
Un cambio di generazione/link interrompe il lavoro diagnostico e può impedire
cleanup; il backend conserva la propria gestione normale, senza modifiche.

Log separato `/dev/shmem/nbt-aad-test6wb.log`, **nuovo per ogni start accettato**,
con cap 5.000.000 byte e riserva cleanup/footer. `list/status/stop` non lo cancellano.
Recuperarlo prima del prossimo start e prima di spegnimento, perché è in RAM.
Vengono registrati richiesta, piano, payload completi, test/handle, TX tentato e
risultato locale, RX/errori asincroni, pause, interruzione, completamento e cleanup.
Le callback copiano metadata in una coda limitata senza disco; contesa/overflow
possono perdere telemetria. I contatori sono del terminale condiviso: i delta
osservati non attribuiscono necessariamente ogni evento al lavoro diagnostico.

Il limite log ferma nuove prove e tenta cleanup nella riserva; un piano enorme
può esaurirlo già prima degli invii. Esiti job: 0 completamento senza errori
rilevati, 2 log non apribile, 3 errore trasporto/preparazione/esecuzione, 4 cleanup
non confermato o errori osservati, 130 stop/shutdown richiesto. Sono visibili in
`status last_exit`; il codice d'uscita di test6wbctl descrive soltanto la richiesta
IPC (0 accettata/risposta OK, 2 sintassi/file, 3 IPC indisponibile, 4 richiesta rifiutata).

Il cleanup aspetta il dispatch, non gli ACK, e osserva poi le risposte per un
secondo. Risposte successive restano elaborate dal backend ma possono non finire
nel log diagnostico già chiuso. Il log non è un protocollo di transazioni:
`TX_LOCAL_OK`, fine batch e exit 0 **non confermano la visualizzazione fisica**.
`PHOTO_BEGIN/END` delimitano il tempo per fotografare manualmente, non una fotocamera.
La grafica resta asincrona e paced; C04 e registrazione conservano le risposte
obbligatorie del backend esistente. Non vengono attesi ACK grafici singolarmente.

Alla normale chiusura NBT-AAD il modulo ferma IPC/lavoro, tenta la pulizia e
rimuove i propri socket/lock temporanei. Non chiude o distrugge g_most/HudTerminal.
SIGKILL, errori vendor o perdita link possono impedire cleanup/log finale. La
successiva istanza recupera solo il socket stale nel proprio percorso privato,
sotto lock esclusivo: non elimina socket o file estranei.

## Rimozione completa dopo la mappatura

Preferire il revert del commit dedicato all'integrazione (hash nella consegna).
Se il backend è stato modificato successivamente, rimuovere manualmente soltanto:

1. Tutta `milestone-7/src/test6wb_temp/` (engine, IPC/client, copie dei test/log,
   provenienza ed esempi) e questo documento `milestone-7/docs/TEST6WB_TEMP.md`.
2. Tutti i blocchi delimitati `TEST6WB_TEMP_BEGIN` / `TEST6WB_TEMP_END` nei sette
   file esistenti:
   - `milestone-7/Makefile`
   - `milestone-7/CMakeLists.txt`
   - `milestone-7/src/main.cpp`
   - `milestone-7/src/cluster/nav_core.hpp`
   - `milestone-7/src/cluster/nav_core.cpp`
   - `milestone-7/src/cluster/hud_terminal.hpp`
   - `milestone-7/src/cluster/hud_terminal.cpp`
3. Sul PC eliminare gli output temporanei con `make TEST6WB_TEMP=1 test6wb-temp-clean`
   **prima** di rimuovere i blocchi Makefile; poi compilare la normale variante
   `nbt-aad` con le impostazioni originali. Nessun riuso dei vecchi object temporanei.
4. Distribuire il normale daemon con la propria procedura, rimuovere manualmente
   helper e file temporanei dall'NBT dopo aver recuperato log/fotografie.

La rimozione elimina il codice, non lascia una feature solo disabilitata. Fuori
questi blocchi nessuna logica o payload di produzione è modificato. I Core,
codec, MOST/DSI e gli altri sorgenti del backend rimangono quelli originali.
Non rimuovere parti successive del backend non marcate come temporanee.

## Verifiche della consegna

Controlli soltanto statici: provenienza esatta dei builder/catalogo, delimitatori,
include, inventario dei due build system, esclusione predefinita, assenza di nuove
aperture/registrazioni MOST, validità dei blocchi rimovibili e invarianti del
trasporto. Nessuna compilazione, esecuzione nativa/QNX o prova sulla vettura.
Le prestazioni e l'IPC AF_UNIX sulla specifica head unit devono essere confermati
dalla compilazione Windows/QNX e dalle prove controllate dell'utilizzatore.
