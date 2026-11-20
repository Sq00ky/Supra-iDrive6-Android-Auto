# Diagnostica AmiGO e avvio immediato 6WB

Intervento sui sorgenti del 18 novembre 2026. Nessuna compilazione, installazione, operazione SSH, commit o push. Le modifiche locali precedenti al renderer sono state conservate.

## Attesa iniziale individuata e rimossa

`BmwResources::advance()` in `src/cluster/hud6wb_resources.hpp` sottoponeva il batch `6wb-size-check`, contenente otto chiamate C04 `ImageGetSize`. Restava poi in `kQuerying` fino a `port.waitIdle(0)`, impedendo al display di iniziare il disegno. La configurazione 6WB in `nav_core.cpp` usa `maxInFlight=1` e `sizeReplyMs=5000`: in assenza di risposte, le otto chiamate possono occupare circa 40 secondi, oltre agli eventuali tempi di disponibilita del collegamento. Non e presente un unico sleep da un minuto; la durata effettiva della prova precedente non e dimostrabile dal solo codice.

Il percorso attivo ora:

- considera immediatamente verificate le dimensioni del profilo misurato;
- restituisce un batch di interrogazione vuoto, anche nel percorso shadow;
- non chiama `imageSize()`, `getSize()`, `submit()` o `waitIdle()` per verificare le superfici;
- non torna a interrogare le dimensioni dopo una disconnessione;
- richiede comunque `port.status()==kReady`, mantenendo host HUDTerminal, collegamento MOST/DSI, pacing, ACK, gestione degli errori e recupero del trasporto.

Il codice originale della classe e conservato integralmente come commenti, sotto `ORIGINAL C04 DISCOVERY`. Non e un fallback eseguibile. Le funzioni C04 generiche del trasporto restano disponibili per un futuro ripristino esplicito; nessun batch C04 viene prodotto dalla normale esecuzione 6WB. Le attese di cleanup/trasporto non dipendenti dalla discovery non sono state rimosse.

Dimensioni utilizzate direttamente: `0x17` 309x330; `0x1F/0x20` 221x158; `0x18/0x19` 305x44; `0x23/0x24` 283x40; `0x35` 28x113. Il profilo e il renderer Work non sono stati modificati.

## Percorso Android Auto e filtri

Il servizio NAV viene pubblicato sul canale `AA_CH_NAV_STATUS` dalla configurazione locale. Dopo decifratura e ricomposizione, `AASession` acquisisce il corpo originale prima del dispatcher/callback. Il canale effettivo viene ora passato al recorder anziche scritto come costante nell'export.

La callback NAV inoltra gli ID >=0x8000 senza selezionare l'applicazione. `NavMessages::store()` riconosce 0x8003/04/05/06/07: gli altri ID non aggiornano il parser, ma vengono acquisiti dalla diagnostica prima di questo filtro. Non sono stati trovati filtri basati sull'identita Google Maps/AmiGO. Nomi come `mapsDisplay()` indicano una conversione delle distanze, non un controllo dell'app sorgente. I messaggi frammentati incompleti continuano a essere scartati per integrita del trasporto; sono ora annotati con `aa_fragment_drop` (result 1: LAST senza FIRST; result 2: lunghezza ricomposta insufficiente).

`NavMessages`, le definizioni protobuf e la logica delle rotatorie non sono stati modificati. Le uniche aggiunte al percorso del parser sono osservazioni e metadati temporali.

## Attivazione e posizione dei file

In `nbt-aad.env`, mantenendo gli altri parametri della vettura:

```ini
NBT_NAV_DIAG=1
NBT_NAV_DIAG_TEXT=0
NBT_NAV_DIAG_SOURCE=amigo
```

Il JSONL resta in `/opt/aa/logs/nbt-aad-nav-diag.jsonl`, con rotazioni `.1` e `.2` e budget totale preesistente di 15 MiB. `NBT_NAV_DIAG_TEXT=0` e il default consigliato per questa acquisizione: i numeri di manovra/uscita/angolo/distanza rimangono disponibili. `NBT_NAV_DIAG_TEXT=1` conserva l'opzione preesistente di esportare anche testo noto, che puo contenere nomi stradali, istruzioni e destinazioni.

`source` e sempre un'etichetta manuale, non un'identita rilevata. Sono accettati `maps`, `waze`, `amigo` e `tomtom_amigo`; in assenza di una scelta valida viene usato `unspecified`. I record dichiarano `source_kind: "manual_label"` e `application_id: null`.

Per cambiare l'etichetta tra due navigazioni senza riavviare NBT-AAD, l'operatore puo predisporre il file `/opt/aa/logs/nav-diag-source` contenente soltanto una delle etichette accettate. Il writer lo legge all'inizio di ogni navigazione. Prepararlo prima di avviare il percorso: e un'annotazione dell'operatore letta dal writer, non una prova di quale app abbia emesso i messaggi. Se il file non esiste o non e valido, vale il parametro dell'ambiente. Nessun file sulla vettura e stato creato da questo intervento.

## Record JSONL nuovi

### `navigation_session_start`

Emesso quando il messaggio 0x8003 ricevuto passa a ACTIVE (1) senza una navigazione diagnostica gia attiva. ACTIVE ripetuto non apre altre sessioni; il ricalcolo (3) conserva la sessione. INACTIVE/UNAVAILABLE (0/2) o fine connessione AA la chiudono. Un successivo ACTIVE apre una nuova sessione nello stesso processo e nello stesso file.

Campi principali:

- `navigation_session_id`: combinazione di PID, monotono del run, orologio catturato, connessione AA e contatore di navigazione. Non dipende dalla sola ora assoluta.
- `mono_ns`: monotono acquisito alla ricezione di quel messaggio, non all'avvio del processo o all'esportazione.
- `wall_time`: ISO 8601 UTC con `Z`, ad esempio `2026-11-18T09:15:32Z`; `wall_time_nsec` conserva la frazione.
- `wall_time_valid` e `wall_time_quality`: controllo di disponibilita di CLOCK_REALTIME, intervallo plausibile 2020-2100 e conversione UTC. In caso di errore/data implausibile: `wall_time: null`, validita falsa, qualita `unavailable_or_implausible`.
- Con data plausibile: qualita `system_clock_plausible_unverified`. Questo controllo non dimostra sincronizzazione NTP/GPS o correttezza dell'orologio della BMW: quella verifica richiede il confronto reale sulla vettura. Nessuna data e ricostruita dal tempo di avvio.

Restano i vecchi eventi `session_start/session_end`, che identificano la connessione Android Auto. Il campo legacy `session` mantiene questo significato. La nuova sessione di navigazione e distinta tramite `navigation_session_id`; viene aggiunto anche `navigation_session_end`.

Se il telefono non trasmette una transizione di stato utilizzabile, non viene inventato un inizio basandosi su una manovra o sul presunto produttore. I messaggi rimangono acquisiti, con sessione di navigazione eventualmente nulla: questa eventualita deve essere valutata nelle acquisizioni AmiGO.

### `navigation_raw`

Per ogni messaggio NAV catturato: `mono_ns`, `channel`, `message_id`, `known_message`, `original_bytes`, `captured_bytes`, `body_truncated`, `raw_body_hex`, `raw_exact`, `wire_valid`, `redacted_ranges`. La dimensione riguarda il corpo protobuf, senza i due byte del tipo messaggio, registrato separatamente.

Il corpo viene copiato prima del parser. L'exporter conserva i byte originali dei valori scalari, anche sconosciuti, e maschera in posizione testo e payload length-delimited opachi, riportando offset/lunghezza delle maschere. Non introduce interpretazioni di geometria o identita. Il corpo mascherato non va riprodotto come messaggio originale: `raw_exact=false` lo segnala. Gli unknown length-delimited restano mascherati anche abilitando il testo, per evitare di riversare stringhe/blob non identificati. Per tali campi si conservano struttura, dimensioni e indicazione della redazione; una geometria codificata in un blob sconosciuto non e quindi ricostruibile con questa acquisizione.

Rimangono inalterati i record `navigation` e il decoder diagnostico esistente, con tipo manovra, numero uscita, angolo, corsie, distanze e struttura dei campi. Audio, video, canali di autenticazione e credenziali non sono acquisiti. Limite preesistente: 8192 byte di corpo per record; eventuale troncamento e perdita di coda vengono dichiarati.

### `navigation_timing`

Stadi osservati: `aa_receive`, `parser_enter`, `state_store_done`, `snapshot_decode_begin`, `snapshot_decoded`, `display_submit_begin/end`, `most_send_begin/return`. Gli ultimi due riguardano comandi di presentazione 6WB selezionati: switch buffer, testo e barra. Il valore di ritorno di send indica l'esito della chiamata DSI, non la ricezione fisica o la visualizzazione sul quadro. ACK e risposte tardive continuano a essere gestiti dal codice preesistente.

`rx_sequence` e `rx_mono_ns` correlano callback, snapshot e chiamate display con l'ultimo messaggio acquisito per quello snapshot. Uno snapshot puo combinare manovra e distanza provenienti da messaggi diversi: non viene affermato che tutti i suoi campi provengano dall'ultimo pacchetto. Gli eventi MOST hanno `correlation: "timeline_only"`, senza attribuire a un vecchio comando gia accodato il numero dell'ultimo messaggio AA. `most_function`, `handle` e `result` descrivono l'invio senza registrare payload MOST.

Le due code diagnostiche sono indipendenti; l'ordine fisico nel file non e necessariamente quello temporale tra record raw e timing. Usare `mono_ns` per la timeline. La ricezione osservata e quella del messaggio AA completo dopo decifratura/ricomposizione: non e il timestamp di emissione dell'app sul telefono. Un ritardo precedente a questo punto non puo essere separato in app/rete/telefono usando soltanto questi log.

## Carico e compatibilita

Il producer principale continua a fare copie limitate e trylock, senza parsing diagnostico, I/O su disco o attese per il writer. Sono riservati quattro posti della coda agli stati/boundary rispetto ai messaggi NAV ordinari. La traccia multithread usa una seconda coda fissa da 128 metadati, trylock e nessun payload. Parsing wire, formattazione JSON, lettura dell'etichetta e scrittura restano nel thread diagnostico esistente. Nessuna nuova libreria o thread.

La diagnostica e best effort: in caso di contesa, saturazione o errore I/O puo perdere record e non blocca la navigazione. I contatori legacy `lost/truncated` restano disponibili; `trace_queue_full_drops` conta la saturazione della traccia, non gli scarti per contesa del trylock. L'assenza di un evento timing, da sola, non prova un messaggio mancante nel percorso funzionale.

## Analisi locale

`tools/analyze_nav_diag.py` continua a leggere il vecchio formato e ora riepiloga anche sessioni di navigazione, data di inizio, etichetta manuale, campioni numerici di rotatorie e massimi ritardi correlati alla ricezione per stadio. Non stampa nomi stradali o destinazioni. Per le rotazioni passare `.2`, `.1`, file corrente, in ordine cronologico; includere solo i file effettivamente presenti. Esempio per un singolo file copiato manualmente sul PC:

```powershell
python -B tools/analyze_nav_diag.py 'C:\percorso\acquisizione-amigo.jsonl'
```

L'analizzatore riporta al massimo 2000 campioni di rotatoria per sessione nel riepilogo; i record originali restano nel JSONL entro il budget di rotazione. Nessuna correzione degli angoli o delle rotatorie e stata applicata.

## File e verifiche

Sorgenti: `src/aa_session.cpp`, `src/nav_diag.hpp`, nuovo `src/nav_diag_trace.hpp`, `src/cluster/nav_core.cpp/.hpp`, `src/cluster/hud_terminal.cpp`, `src/cluster/hud6wb_resources.hpp`. Strumento: `tools/analyze_nav_diag.py`. Test aggiornati: `test_hud6wb.cpp`, tre contratti statici risorse/trasporto/recupero, `test_nav_diag_persistence.py`, `test_nav_diag_analysis.py`; nuovo `test_nav_amigo_static.py`.

Risultati: 50 casi distinti Python/statici passati (29 6WB, 21 NAV/analisi/nuovi contratti), comprendenti fixture di piu navigazioni nello stesso file, clock ISO, rotatorie e correlazione temporale. Verifiche mirate rieseguite dopo gli ultimi aggiornamenti. Nessun test C++ o esecuzione del daemon; i modelli Python non dimostrano il comportamento del binario QNX.

Verificati anche: manifest dei 12 file autorevoli Work invariato; nessuna modifica al parser `nav_msg.hpp` o ai driver 6WA/J29; delimitatori C++ e inclusioni locali; assenza di probing nel codice 6WB attivo; `git diff --check`; assenza di nuove dipendenze/unità di build.

La procedura manuale QNX esistente resta invariata (`build-6wb.cmd`). La compilazione e il comportamento reale restano da verificare dall'utente. Nessun cambiamento della working tree preesistente e stato annullato, nessun binario o file OEM modificato.
