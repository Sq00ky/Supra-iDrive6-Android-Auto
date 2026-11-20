# AndroidX, rotatorie AmiGO e compatibilita dei navigatori

Analisi del 18 novembre 2026. Nessuna compilazione, installazione o commit. Le acquisizioni originali non sono state modificate.

## Risultato applicato ai sorgenti

Il confronto copre tutte le 49 costanti attualmente dichiarate da AndroidX: 0-29 e 32-50. I valori 30/31 non compaiono nel modello AndroidX corrente e rimangono compatibilita legacy del progetto, senza reinterpretazioni.

Riferimenti primari:

- https://developer.android.com/reference/androidx/car/app/navigation/model/Maneuver
- https://github.com/androidx/androidx/blob/androidx-main/car/app/app/src/main/java/androidx/car/app/navigation/model/Maneuver.java

I tipi gia gestiti 0-46 erano correttamente associati agli artwork, inclusi 39 destinazione, 32/33 rotatoria completa CW, 34/35 completa CCW, 43/44 ingresso/uscita CW, 45/46 ingresso/uscita CCW. Nessuna inversione introdotta o corretta arbitrariamente. L'angolo documentato indica quanto percorrere lungo la rotatoria; il numero e contato dall'ingresso nel senso di percorrenza. Non sono stati trasformati in angoli assoluti o dedotti dal traffico del paese.

Differenza implementabile trovata: 47/48 sono traghetto stradale a sinistra/destra, 49/50 traghetto ferroviario a sinistra/destra. Gli artwork Work esistevano gia, ma l'adattatore li escludeva.

Modifiche produttive limitate a:

1. `src/cluster/hud6wb_kit.cpp`: quattro associazioni ai ruoli esistenti `FerryLeft`, `FerryRight`, `TrainFerryLeft`, `TrainFerryRight`; limite di accettazione centrale da 46 a 50. Nessuna modifica alla funzione di encoding, alle primitive o agli algoritmi grafici.
2. `src/cluster/nav_msg.hpp`: limite del secondo step da 46 a 50. Il tipo primario era gia copiato dal messaggio senza quel limite; nessun nuovo campo o struttura normalizzata.

Tutto il codice del parser, salvo quell'unico limite, e identico al checkpoint precedente; tutte le associazioni 0-46 sono identiche. Non sono stati aggiunti stato di attesa, buffering, timer o selezioni basate su applicazione/etichetta. Gli altri driver non sono stati modificati. Distanze, corsie, cartelli, aggiornamenti, discovery disattivata, superfici, display e trasporto MOST/HUDTerminal restano invariati.

Gli algoritmi e i dati grafici Work rimangono identici all'archivio originale. Il manifest di importazione non e stato riscritto. `6wb-androidx-adapter-overlay.json` descrive separatamente l'esatta estensione dell'adattatore: il controllo di integrita annulla solo questi cambiamenti noti e confronta il resto con gli hash originali, evitando di dichiarare falsamente identico il file adattatore modificato.

## Cosa dimostra il log AmiGO

File: `C:/Users/memel/BMW-6WB/nbt-aad-nav-diag-amigo.jsonl`.
SHA-256: `1d6cfad8816aa881c5a2641c771e8976101887159727322c0ca0afd01e678557`.

4670 righe JSON valide; 208 messaggi NAV: una apertura canale 0x0007, 8 stati 0x8003, 87 istruzioni 0x8006, 112 distanze 0x8007. La navigazione dura circa 360 secondi e inizia alle 09:37:25 UTC / 10:37:25 italiane secondo l'orologio registrato, plausibile ma non verificato esternamente.

Tra le istruzioni: 20 ingressi CW tipo 43 e 5 ingressi CCW tipo 45. Nei rispettivi sotto-messaggi maneuver e presente solo il campo type, senza numero o angolo. Ogni istruzione contiene un solo step: nessuna seconda manovra da usare come uscita anticipata. Non compaiono tipi completi 32-35 o uscite 44/46, nemmeno nei messaggi successivi. Le altre manovre osservate sono 1, 6, 7, 8 e 39.

Con questi dati non e possibile ricostruire un'uscita. Non e corretto convertire automaticamente un ingresso in rotatoria completa, contare le uscite dagli aggiornamenti di distanza, interpretare un cambio di senso come uscita o invertire CW/CCW per adattarlo al paese. Il percorso Work gia esistente rappresenta genericamente l'ingresso senza inventare numero/angolo: non e stato modificato.

**Correzione di stato 43-46 non applicata:** il caso non e esclusivo di AmiGO. Nel log Waze ci sono 47 istruzioni tipo 45, incluse quelle prive di uscita/angolo, e 3 tipo 46; nel log Maps ci sono 13 tipo 46. Cambiare questi stessi dati, azzerarne i campi oppure mantenere informazioni di una rotatoria precedente modificherebbe anche il comportamento attuale di Maps/Waze. Il vincolo di non regressione impedisce una trasformazione di questo genere. Non e stato aggiunto un normalizzatore senza effetto o basato sull'app.

Per completare un eventuale trattamento separato ingresso/uscita servono acquisizioni in cui l'uscita sia realmente trasmessa, o un altro dato documentato che la identifichi. In questa acquisizione il dato manca, non risulta scartato dal parser. Anche conoscere il nome dell'app non permetterebbe di ricavarlo.

## Identita del navigatore e autovelox

Non e stato identificato un campo affidabile di identita dell'app nel canale NAV gestito o nei messaggi esaminati. `source` e un'etichetta manuale; non seleziona alcun parser. Non e stata utilizzata per riconoscere AmiGO nel codice funzionale.

Non sono presenti ID o campi NAV dedicati agli autovelox nell'acquisizione. Le sole due occorrenze di campi sconosciuti appartengono a 0x0007, riconosciuto dal dispatcher come ChannelOpenRequest. Non sono allarmi autovelox.

I campi `cue.text` delle 87 istruzioni sono tutti mascherati: non si puo escludere un avviso testuale in quel campo. Una nuova acquisizione mirata con `NBT_NAV_DIAG_TEXT=1`, passando vicino a un autovelox noto e annotando l'ora, permetterebbe di verificarlo. Questa opzione registra anche testo di navigazione: non e stata attivata automaticamente. Un avviso visibile sul display Android Auto o pronunciato dall'app non dimostra la presenza di un identificativo autovelox nel canale NAV del quadro.

TomTom conferma gli avvisi autovelox nella presentazione di AmiGO su Android Auto, senza documentare un identificativo wire per il quadro:
https://www.tomtom.com/newsroom/press-releases/general/26911/tomtom-amigo-now-available-on-android-auto/

## Ritardo delle indicazioni

Analisi completa numerica in `6WB_AMIGO_TIMING.json`, correlata all'hash del log.

| Misura | Risultato osservato |
|---|---|
| Ricezione -> ingresso parser / memorizzazione stato | massimo circa 1 ms |
| Ricezione dell'ultimo input -> snapshot decodificato | mediana 129,5 ms, p95 236 ms, massimo 253 ms |
| Durata tra inizio/fine decodifica, 144 coppie | sotto la risoluzione osservata del clock |
| Durata delle chiamate al display, 1458 coppie | massimo circa 4 ms |
| Durata delle chiamate DSI osservate, 212 coppie | massimo circa 1 ms |

Due transizioni rappresentative:

| Sequenza AA / tipo | Inizio chiamata display dalla ricezione | Switch centrale successivo dalla ricezione |
|---|---|---|
| 32 / ingresso CW 43 | 134 ms | circa 3225 ms |
| 86 / ingresso CCW 45 | 222 ms | circa 3313 ms |

Gli aggiornamenti manovra possono essere seguiti subito dalla distanza prima del tick: gli snapshot corrispondenti portano le sequenze 33 e 87, non 32 e 86. Non significa che la manovra sia stata ignorata.

Lo switch e una correlazione solo temporale: i log non associano un ID immutabile del frame al comando MOST, quindi potrebbe riguardare un'immagine gia accodata. Non prova l'istante di comparsa fisica di quella nuova manovra. Il divario di circa tre secondi e compatibile con il disegno a molti comandi, la coda e il pacing preesistenti; non e spiegato dalla durata delle singole chiamate DSI o dalla decodifica osservata. Quei meccanismi non sono stati cambiati perche il compito li vincola.

I valori massimi di oltre 80 secondi per `display_submit_* - rx_mono_ns` sono l'eta dello snapshot durante chiamate keepUp ripetute, non un messaggio rimasto 80 secondi in coda. Non vanno interpretati come latenza della prima indicazione.

Non risultano perdite nella coda principale o saturazioni della coda timing; gli eventuali scarti per contesa del trylock non sono contati. Le acquisizioni Maps/Waze disponibili non contengono questi timing: il confronto quantitativo tra navigatori non e possibile con gli stessi punti di misura. Nessun timestamp del log indica quando l'app abbia deciso/emesso la manovra prima della ricezione AA.

## Test e stato finale

Fixture senza testo personale in `tests/fixtures/nav_androidx_captures.json`: 27 combinazioni rappresentative di manovra, che coprono le 87 istruzioni AmiGO, 434 Maps e 227 Waze. Per AmiGO il sotto-messaggio maneuver e estratto dal raw acquisito; per Maps/Waze, privi di raw, i messaggi di test sono ricostruiti esplicitamente dai campi numerici registrati. Non sono spacciati per catture wire integrali. Conservati SHA-256 e provenienza.

`androidx_maneuver_types.json` registra la tabella ufficiale verificata. `nav_androidx_cases.inc` prepara gli stessi campioni per il test C++; aggiunti test del parsing principale/lookahead 47-50 e ampliata la verifica del kit. I test C++ non sono stati compilati o eseguiti.

**42 test Python/statici passati:** 7 nuovi controlli AndroidX/fixture/regressione, 29 controlli 6WB, 6 controlli AmiGO/diagnostica. Per Maps/Waze il controllo verifica che tutto il parser sia identico salvo l'ammissione dei nuovi tipi, che ogni mappatura gia supportata sia identica e che display/renderer siano invariati. Sono prove sui sorgenti e sui dati: nessun confronto di chiamate eseguite da un binario C++ e stato dichiarato.

Nessuna attesa, buffering o dipendenza aggiunti. Solo i due file produttivi elencati sono cambiati in questo intervento. Rimane necessaria la compilazione manuale QNX; la procedura esistente non cambia. Nessun commit o deployment eseguito.
