# Ripristino della navigazione 6WB

La base locale è `a1da480e`, successiva al merge upstream `abd65447`. Il checkpoint di confronto per il rendering centrale funzionante sulla vettura è `4f2670ce`. All'inizio di questo intervento il branch `feature/bmw-6wb` aveva i file tracciati puliti. Le modifiche di questo intervento restano nel working tree.

## Navigazione e barra

Il simbolo centrale riceve sempre la manovra Android Auto corrente. La sola barra `0x35` è visibile con distanza valida fra zero e 500 metri inclusi. Oltre 500 metri, con distanza negativa o assente, viene nascosta. Il valore della barra mantiene la convenzione precedente e usa 500 metri come fondo scala. Il nome strada conserva fino a 255 byte ISO-8859-15 e il percorso 6WB supera il limite condiviso di 40 code point. Un'impostazione esplicita `NBT_6WB_STREET_CHARS` continua a essere rispettata.

Il renderer disegna nel buffer centrale inattivo, lo svuota, invia la geometria, effettua `20E` e rende esplicitamente visibile il buffer disegnato tramite `204`. Aggiorna la previsione A/B solo dopo l'accettazione di tutti i chunk ordinati. La coda mantiene il pacing di 15 ms e gli ACK grafici restano asincroni. `waitIdle(0)` verifica il completamento degli invii locali. Le risposte C04 mantengono una gestione separata.

Un errore di rappresentabilità precedentemente restituiva `kInternal` e il core arrestava la guida per la sessione. Ora un poligono collineare, senza area dopo l'arrotondamento, viene ignorato. Una manovra ancora irrappresentabile viene segnalata e la relativa immagine precedente viene nascosta, conservando nome strada e distanza e permettendo le manovre successive. Una geometria di corsia non rappresentabile nasconde la riga corsie e lascia proseguire il centro. Artwork, palette e coordinate dei simboli conservano i sorgenti precedenti.

Questi sono percorsi verificabili nel codice. I log locali di navigazione risalgono al 14 novembre e non documentano la nuova prova stradale. L'attribuzione del blocco osservato a un errore di geometria richiede il log della build installata. Nel diff da `4f2670ce` la coda è passata agli slot upstream, mentre il meccanismo grafico asincrono e la cadenza sono rimasti presenti.

## TX e superfici superiori

`NBT_6WB_TX=4` conserva il trasporto predefinito per primitive, testi, visibilità e C04. `NBT_6WB_TX=0` seleziona esplicitamente il trasporto alternativo. Gli altri valori vengono ignorati. I bitmap mantengono il proprio TX0 già esistente. Le osservazioni di Pacanaz riguardano la sua F31 e includono ACK TX0 inviati a HmiMain `0x2020`. Il nostro rendering non dipende dall'arrivo di questi ACK. L'arbitraggio OEM resta quello del backend 6WB precedente.

I bitmap su `0x18/0x19` sono disabilitati per impostazione predefinita dopo l'esito della prova dell'utente. Il codice normale può essere riabilitato esplicitamente con `NBT_6WB_ICON_BITMAPS=1`. Richiede entrambe le misure C04 compatibili e conserva gli handle distinti. Svuota il buffer inattivo, carica il bitmap sullo stesso handle, effettua lo scambio verso quell'handle e ne imposta la visibilità. Alla rimozione del secondo passo nasconde entrambi i buffer. C04 misura le dimensioni e non prova la visibilità del contenuto. La causa dell'assenza di immagini su ciascuno dei due handle rimane aperta.

Le tre prove grafiche iniziali, il clock di calibrazione, i bitmap di prova e gli automatismi di avvio sono rimossi. Restano le funzioni bitmap utilizzate dalla miniatura opzionale.

## Diagnostica C04

La configurazione attuale interroga esclusivamente il terminale registrato `82/00`, sugli handle `17`, `1F`, `20`, `18`, `19`, `23`, `24`, `35`. Non apre `82/02`. Ogni richiesta attende al massimo 5000 ms. Il superamento della soglia diagnostica di 3000 ms non invia retry e non sposta il termine finale. Una risposta non valida, un ErrorAck correlato, un errore locale di invio o un timeout rilasciano la richiesta C04 e fanno proseguire la coda. Una ripetizione sulla stessa registrazione viene soppressa e registrata, per evitare di attribuirle una vecchia risposta. Le risposte tardive vengono registrate separatamente.

Il file predefinito nell'installazione manuale è `/opt/aa/logs/6wb-surfaces.log`, coerente con il log NAV esistente. Nell'installazione NBTApps si usa `<dataDir>/logs/6wb-surfaces.log`. Si utilizza il writer rotante esistente, con tre generazioni e budget totale di 768 KiB. Il file viene aperto in append, con permessi 0600. Sono registrati UTC quando disponibile, clock monotono, terminale, peer, handle, tag, destinazione, operazione, payload esadecimale, dimensioni restituite, tempo trascorso ed esito. Gli eventi di registrazione e selezione terminale separano le sessioni. Il writer esegue I/O sul thread di invio, fuori dal mutex del core. Il callback di ricezione accoda solo record limitati. Un errore di scrittura viene segnalato sul log ordinario e lascia continuare la guida.

La scrivibilità è controllata sul sistema QNX al primo append. Questa sessione locale non accede alla vettura. Il mount di `/shmem` sulla vettura resta da verificare. Il percorso standard QNX `/dev/shmem` risiede in RAM e il progetto lo usa per file temporanei. Per questo il log diagnostico usa il percorso su filesystem dell'installazione. Il writer effettua `fflush` per record, con gli stessi limiti di durabilità del log NAV in caso di perdita improvvisa di alimentazione.

Dopo il completamento delle richieste, dimensioni centrali ricevute e incompatibili impediscono il disegno sul profilo sbagliato. Se manca una risposta centrale, si usano i riferimenti del profilo funzionante, senza inserirli nella cache delle misure né registrarli come risultati OK. Le superfici opzionali delle corsie e dei bitmap richiedono misure compatibili. La barra può usare il riferimento noto quando manca la misura, e viene disabilitata se riceve una misura incompatibile. L'avvio richiede solo le richieste reali e i loro timeout, senza una pausa fissa aggiuntiva.

## Verifiche

Sono disponibili contratti statici Python e modelli numerici per soglia inclusiva, invii asincroni, sequenza dei buffer, rimozione dei test, persistenza, gestione C04, mantenimento dell'artwork e nomi strada. Questi controlli non eseguono il codice C++ e non sostituiscono compilazione o prova sul quadro. La compilazione manuale rimane a cura dell'utente.

Riferimenti usati nel confronto

- [Commento Pacanaz nella PR 179](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/pull/179#issuecomment-6900388777)
- [Renderer JayJay](https://github.com/JayJay3071/nbt-android-auto/blob/main/src/android_auto/knob/nbt_cluster.cpp)
- [Filesystem QNX SDP 6.5](https://get.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_sys_arch/fsys.html)

## Esiti locali e file interessati

Sono passati 45 controlli Python pertinenti. Dopo l’ultima modifica al logging C04 sono stati rieseguiti i dieci contratti di ripristino, tutti superati. Sono stati verificati i delimitatori lessicali di 16 file C++, la coerenza delle liste Make/CMake/test e git diff --check. I test C++ aggiornati restano da compilare ed eseguire manualmente.

- Modificato `milestone-7/src/cluster/display.hpp`
- Rimosso `milestone-7/src/cluster/hud6wb_calibration.hpp`
- Modificato `milestone-7/src/cluster/hud6wb_codec.cpp`
- Modificato `milestone-7/src/cluster/hud6wb_codec.hpp`
- Modificato `milestone-7/src/cluster/hud6wb_config.hpp`
- Modificato `milestone-7/src/cluster/hud6wb_display.cpp`
- Modificato `milestone-7/src/cluster/hud6wb_display.hpp`
- Modificato `milestone-7/src/cluster/hud6wb_kit.cpp`
- Modificato `milestone-7/src/cluster/hud6wb_kit.hpp`
- Modificato `milestone-7/src/cluster/hud6wb_profile.hpp`
- Modificato `milestone-7/src/cluster/hud6wb_resources.hpp`
- Modificato `milestone-7/src/cluster/hud_terminal.cpp`
- Modificato `milestone-7/src/cluster/hud_terminal.hpp`
- Modificato `milestone-7/src/cluster/nav_core.cpp`
- Modificato `milestone-7/src/cluster/nav_core.hpp`
- Modificato `milestone-7/tests/Makefile`
- Modificato `milestone-7/tests/test_hud6wb.cpp`
- Rimosso `milestone-7/tests/test_hud6wb_calibration.cpp`
- Rimosso `milestone-7/tests/test_hud6wb_calibration_static.py`
- Rimosso `milestone-7/tests/test_hud6wb_dual_static.py`
- Modificato `milestone-7/tests/test_hud6wb_geometry_static.py`
- Modificato `milestone-7/tests/test_hud6wb_kit.cpp`
- Modificato `milestone-7/tests/test_hud6wb_kit_static.py`
- Modificato `milestone-7/tests/test_hud6wb_surface_static.py`
- Aggiunto `milestone-7/tests/test_hud6wb_recovery_static.py`
- Aggiunto questo documento `milestone-7/docs/6WB_NAV_RECOVERY.md`
