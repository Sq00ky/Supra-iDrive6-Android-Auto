# BMW NBT EVO ARM/QNX e KOMBI 6WB

Reverse engineering OEM e handoff per il backend Android Auto. Veicolo di riferimento BMW F31 LCI 2018, NBT EVO ID6 e 6WB. Il resoconto locale della cattura indica `NBTevo_V20153I`. Il codice applicativo citato è quello del checkout `4f2670cea73ee1a45453f102095e87020f77e4f2`. Nel documento, i percorsi locali partono da `C:/Users/memel/BMW-6WB` salvo indicazione diversa.

## 1. Provenienza e inventario

- **CONFERMATO DA CODICE ARM** indica istruzioni, strutture, tabelle, simboli o stringhe con riferimenti verificati negli ELF ARM.
- **INFERITO** indica una conclusione sul significato o sulla composizione a partire da quei riscontri.
- **DA VERIFICARE SUL VEICOLO** indica una proprietà specifica del 6WB F31 che richiede un riscontro del ricevitore.
- **FONTE ESTERNA** indica un risultato di JayJay x86 o del progetto Sq00ky, con il veicolo indicato quando noto.
- **OSSERVATO F31** indica una cattura o un resoconto locale. **CODICE PROGETTO** indica comportamento verificato nei sorgenti, anche quando la corrispondente revisione non è stata provata sul veicolo.

Gli indirizzi esadecimali nelle sezioni ARM sono **indirizzi virtuali ELF**, non offset di file o indirizzi runtime con load bias. Per convertirli usare un `PT_LOAD` contenente l'indirizzo nella parte file-backed e calcolare `p_offset + VA - p_vaddr`. Le etichette delle funzioni interne derivano da stringhe di trace, vtable o call graph. Alcune classi in `NavNBTEvoController` non hanno un simbolo esportato.

| Reperto locale | Byte | SHA-256 locale |
| --- | ---: | --- |
| `NavNBTEvoController` | 16822984 | `4d56522dc47f1361ae7598a628f57c4fe9d3b8843b73c00199343d31d1d5bd22` |
| `HMI/hmi/ID5/bin/HmiMain3D` | 57666756 | `9fdc85e0636845512e6e5a138e6a346829c3dd84b16555bdf3c1eb2d8bdd1161` |
| `HMI/hmi/ID5/data/ro/hmiplugins/libhmiremoting.so` | 7175484 | `659c5f6c789fc16cdd87b79ec82c59d6e8d7ca88413375498db8c023e59a284c` |
| `libimp_sys_most_base_nbt.cpp-ne.so.17` | 1302568 | `63f534151854b50e9a8fd4cdd19f4d8837828d6c9a15ee9097dc97d609f3bc8f` |
| `libsys_dsi_base.cpp-ne.so.4` | 38776 | `70f2182907599850094b013cd5794853cbc6517be4c24203659b73ec46d20896` |
| `libsys_dsi_base.gpp.so.4` | 38792 | `6a25bdb99fdfabccf5a8189c1fc21b5f74083229d8db179110357f64300871ed` |
| `libsys_dsi_colibry.cpp-ne.so.8` | 299684 | `adec5e17c4f4f4826de2eeee2aa2f3c5bf27de7e8edc3e107b79fa2ca6a88c10` |
| `libsys_dsi_colibry.gpp.so.8` | 336408 | `f559c5c1455ed62e376ec72fdb77d5211b748bc9167f9c6866083db1515d19b4` |
| `libsys_dsi_commonapi21.gpp.so.2` | 212612 | `4fcd5a0f8d9c4845975b3e02ab88b1ec6cdd5725b82dce7f9cbfae47a4a4aafe` |
| `libsys_dsi_commonapi21.so` | 212612 | `4fcd5a0f8d9c4845975b3e02ab88b1ec6cdd5725b82dce7f9cbfae47a4a4aafe` |
| `libsys_dsi_servicebroker.cpp-ne.so.4` | 16112 | `500266be0e43305eb7904335daab78ee5ecac3ab8f59ebb3561de09ac030f011` |
| `libsys_dsi_servicebroker.gpp.so.4` | 16700 | `d8ff360c85590788f64a6e9eaffd0749a66bc2c338b8b6585d940d5148980e4d` |
| `libsys_dsi_trace.cpp-ne.so.6` | 82136 | `714a973dd3d2ba47bf65e0c78df3cb622600449e83bc8236359964b1097ecb2f` |
| `libsys_dsi_trace.gpp.so.6` | 95688 | `f8606d7932c3a61be577c302c7426760c3d02d447be3e94e3473ddfaf8ea9ce9` |

Tutti gli ELF in tabella sono ARM32 little-endian. `HmiMain3D` è `ET_EXEC`, `libhmiremoting.so` è `ET_DYN`. `NavNBTEvoController` è un eseguibile QNX `ET_EXEC` con entry `0x0012AC60`, 2163 voci dinamiche e tabella simboli completa assente. È stato acquisito da `/opt/nav/eceusa/bin/NavNBTEvoController`. La libreria MOST è `ET_DYN`, con 5778 voci dinamiche, acquisita da `/opt/sys/lib/libimp_sys_most_base_nbt.cpp-ne.so.17`. Le librerie DSI sono `ET_DYN`. Le dieci copie sono byte per byte identiche alle rispettive porzioni di `DSI_transfer.bin`, SHA-256 `57ae6be437dfe7c6edd5ec8e5c878782665ef888d7fbd6585f74dab6f9e94554`. I due nomi CommonAPI contengono gli stessi byte. I percorsi OEM individuali dei file DSI e gli hash sul sistema sorgente non sono attestati dal contenitore. Di `HmiMain3D` e `libhmiremoting.so` sono disponibili i percorsi nell'albero HMI, mentre il mount originale non è documentato nei reperti usati.

- **Controller.** Contiene il viewer di controllo, `CIcHudComm_TF11_13`, parser di memoria condivisa, schema HUDTerminal e builder ARM. Importa direttamente le varianti `cpp-ne` DSI Colibry, ServiceBroker e Trace, la libreria MOST, lo stack Colibry/MoCCA/SOME-IP, oltre alle dipendenze QNX. Le varianti DSI `gpp` sono build distinte. Il set delle librerie DSI supporta il trasporto e il binding, mentre i serializzatori HUDTerminal verificati appartengono al controller e alla libreria MOST.
- **Libreria MOST.** `CHBMOSTClientImpl::informationProcessMessage` a `0x0010C1B8`, `requestServerProcessMessage` a `0x0010C360`, `CDSYSMOSTServerProxy::requestProcessMessage` a `0x00125E98`, `CHBLogFuncMethodUnclassified::sendAckEth` a `0x000EB6A4`, `CHBParaStream::writeToStream` a `0x0011AFC0`.
- **HMI.** `HmiMain3D` espone il proxy `CPxyHUD_Terminal` e il descrittore KOMBI della navigazione. La ricerca mirata nei 50 ELF fra HMI, OTF, plugin e librerie acquisite ha trovato il nome `NextManeuverDescriptorKombiDisplay` anche in `libhmiremoting.so`, rispettivamente agli offset file `0xF068E` e `0x5E87E7`. Il nome documenta una proprietà remota. Il percorso grafico dimostrato si trova nel nuovo controller.
- **Materiale pregresso.** Sono stati consolidati il rapporto della sessione locale, i riscontri di disassemblaggio disponibili, `milestone-7/docs/BMW_6WB.md`, la documentazione vettoriale e palette, i sorgenti e `milestone-7/nbt-aad-6wb-diag.csv`. `BMW_NBT_Navigation_OEM.tar` è un archivio senza membri. Non è stato recuperato un progetto Ghidra completo né un dump del firmware del KOMBI, quindi le asserzioni su quel decoder mantengono la provenienza del progetto o della cattura esterna.

## 2. Architettura OEM

```text
ManeuverView (produttore dei dati e del codec da identificare)
  → sessione displayable + DisplayableData
  → URL/riferimento a memoria condivisa + metadati
  → CGuidanceViewerIcHud in NavNBTEvoController
  → parser chunk → mIcHudComm4Kombi / CIcHudComm_TF11_13
  → istanze logiche → handle HUDTerminal effettivi
  → serializzatore ARM → DSYSMOSTServer → MOST 82/00
  → KOMBI disegna le primitive, visualizza i bitmap e compone le istanze
```

| Stadio | Evidenza nel controller |
| --- | --- |
| Registrazione del displayable | Callback `ManeuverViewConnection` `0x006E1154`. Chiama i rami di selezione `Compressor=NBT_RLE32` a `0x006E1208` e `Compressor=NBT_RLE16` a `0x006E1290`, con trace Bosch/Conti. Registrazione a `0x006E1300`. |
| Ricezione | Callback `DisplayableData` `0x00AC262C`. Le trace identificano URL, dimensione, utente, manovra, tipo, sequenza e display. La callback controlla la memoria condivisa e la sequenza, chiama il parser a `0x00AC28D8` e rilascia il mapping a `0x00AC2900`. |
| Input strutturato | Parser `0x00D4F514` con rami per header, bitmap, strada, distanza, corsie, geometria e descrizione manovra. Parser corsie `0x00D4E038`, chiamata `0x00D4FEE0`, con indice del modello, distanza, intervallo, distanza di anteprima e lista di marcatori. |
| Comunicatore | `initIcHUDcommunicator_IC` `0x006DE344` raggiunge il costruttore TF11–13 `0x0077D498`. Vtable address point `0x00FB3498`, slot `+0x38` dispatcher `0x0077DD70`, slot `+0x34` risolutore nomi `0x0077A5A4`. |
| Geometria | Dispatcher `0x0077DD70` → callsite `0x0077E194` → `0x0077C298` per polilinee e `0x0077E280` → `0x0077C1DC` per poligoni. Punta alla geometria del flusso, seleziona forme e istanze, applica attributi e passa lo stream al builder di `21D`. |
| Bitmap | `0x0077BB94` tratta Road Icon, verifica formato `1` a `0x0077BEA0`. Callsite `0x0077BE80` → `0x00779874` → `0x00774954` → `0x0093E928`. Il buffer è già codificato quando entra nel builder. |
| Strada e distanza | `0x0077DA6C` prepara la strada e `0x0077D718` prepara distanza e unità. Il builder comune è `0x00778B48`. La distanza e l'unità hanno istanze e deduplicazione distinte. |
| Visibilità e completamento | Callback bitmap `0x0077F208` e `0x0077F330`. La seconda raggiunge il percorso di visibilità `0x0077AD6C` a `0x0077F534` in un ramo. Altri rami possono nascondere una coppia in assenza dell'elemento nuovo, ad esempio Road Icon a `0x0077BC70/0x0077BC9C`. |

**CONFERMATO DA CODICE ARM.** La tabella dei ruoli a `0x00D3CEB8` elenca `0 nothing`, `1 road icon`, `2 junction view`, `3 next maneuver arrow`, `4 HG/LG/MiMa`, `5 guiding arrow`, `6 lane guiding`. Sono ruoli di elemento del flusso. `0x0077A2C0` ha un ramo per frame 11 e ruolo 3. Il codice contiene anche chunk `35up_RC_Text`, `35up_SvgImage` e `35up_BitmapImage` a `0x00D50654`, `0x00D50700` e `0x00D5079C`. Il loro utilizzo sulla F31 resta indeterminato.

**Descrittori HMI.** Il lavoro precedente colloca `CRG_NextManeuverDescriptorKombiDisplay` in `HmiMain3D`, con punto di partenza `0x021B7C00` e riferimento `52/00:E7E`. Il limite dello stream è una misura di byte. Gli indirizzi riportati in precedenza per Roadbook `0x01671BD0` e costruzione RIC `0x02422444` sono riferimenti storici, senza un arco dimostrato verso il nuovo percorso grafico. Nel nuovo controller il valore numerico `0xE7E` a `0x00C1BC98` è un offset di struttura, come mostra il successivo `ldrh [r4,r1]`. Nessun arco verificato lega `47F`, `D7E`, `E7E`, Roadbook o RIC al dispatcher HUDTerminal, quindi il backend può lavorare direttamente con testi e primitive.

**Accesso da un processo esterno.** È dimostrato l'accesso a `DSYSMOSTServer`, che il progetto usa già. Manca una procedura verificata per invocare il produttore ManeuverView e fornirgli manovre AA, gestire creazione/stile/privilegi della sessione e interpretarne la memoria condivisa. Il viewer di controllo è incorporato in un `ET_EXEC` e gli indirizzi interni non costituiscono un'ABI di libreria. Generare le primitive nel backend è la strada implementabile con i reperti attuali. Le dipendenze da chiarire per un eventuale riuso OEM sono il servizio produttore, lo schema delle chiamate per l'input di navigazione e gli stili effettivi.

## 3. MOST/DSI e operazioni HUDTerminal

**CONFERMATO DA CODICE ARM.** Nella libreria MOST, `0x0012E420` e `0x0012BE8C` serializzano `MessageType`. `0x0012E284` scrive e `0x0012C0B8` legge i campi dell'header. I tipi DSI e il payload MOST vanno trattati con codifiche separate. La rappresentazione DSI impiegata dal client è questa.

```text
u32le txType
u32le variantTag       # 1 TXControl, 2 TransmissionInfo
u32le variantValue
header {
  u16le dst
  u16le src
  u8 fblock
  u8 instance
  u16le functionId
  u8 operation
  u8[3] padding
  u32le tag
}
u32le payloadLen
u8[payloadLen] payload MOST
```

Per la disposizione esatta del buffer DSI usare `milestone-7/src/most/msg.hpp`. La notifica può essere una variante informativa distinta dalla variante di messaggio. Nel percorso `0x0010C360` la libreria inserisce la sorgente e può accodare o differire una richiesta. `0x0010C1B8` distingue notifiche, messaggi ricevuti e condizioni di riassemblaggio. Il client usa i codici DSI della versione 4.5 `0xF` per registrazione estesa, `0x2` per invio e `0x80000001` per notifica del messaggio, con risoluzione del peer mediante registro. Dispatch nella libreria a `0x0012CCA0/0x0012D984`. Il tag serve al routing e, da solo, non distingue transazioni ripetute. Riferimenti `milestone-7/src/most/ids.hpp`, `msg.hpp` e `client.cpp`.

Per HUDTerminal, `h` indica l'handle effettivo dell'istanza. I campi a più byte del payload grafico sono big-endian. Sorgente e destinazione vengono dalla registrazione e dal registro MOST. Il controller usa `StartResultAck (6)` nei builder grafici verificati. Il KOMBI risponde con `ResultAck (D)`, `ErrorAck (9)` ed eventualmente `ProcessingAck (A)` secondo il metodo. Il runtime ha handler per questi esiti. Le proprietà possono usare `Status (C)`.

| FID | Richiesta HU → KOMBI | Risultati e riferimento |
| --- | --- | --- |
| `C04` ImageGetSize | Schema ARM `[h:u16]`. Il backend F31 invia `[h:u16][01]`. | ARM `[h:u16][w:u16][hgt:u16]`. Costruttore `0x00CA56D0`, schema `0x00CA6E14`, handler `0x00949FE4` con getter `0x00CA0BE8/0x00CA0B98`. **OSSERVATO F31** `00 17 01 35 01 4A`. **FONTE ESTERNA** JayJay riferisce una risposta a sette byte con selettore su x86. |
| `C05` ImageAddBitmap | Header applicativo di 10 byte e stream. | Risultato e associazione per segmento nella sezione 4. |
| `204` SetVisibility | `[h:u16][visible:u8]`, booleano 0/1 nel backend. | Builder `0x00946478`, invio `0x009464EC`. Il parser di risultato ARM `0x00946324` legge handle e visibilità, nominati dalla trace a `0x00946378`. Il CSV F31 include risposte del metodo. |
| `20E` SwitchVisibility | `[hide:u16][show:u16]`, ordine utilizzato nello swap del backend. | `0x00945668`, secondo handle a `0x009456C0`, invio a `0x009456D8`. Il parser ARM `0x0094553C` identifica il risultato come `[sessionHandle:u16][visibleInstance:u16]`, getter `0x00CA1FE4` e trace a `0x0094557C`. Il backend tratta gli ACK con il registro grafico. |
| `210` TextareaSetText | `[h:u16][encoding:u8][stringa][terminatore]`. Il backend 6WB codifica Latin-15 con `01` e terminatore `00`. La forma Unicode del progetto usa `00`, UTF-16BE e `00 00`. | `0x00944F64`; serializzatori MOST `0x0011C238/0x0011B994`. Il matcher del progetto tollera la risposta testo senza terminatore. Codifiche e font accettati dalla specifica revisione dipendono dal ricevitore. |
| `21D` ImageAddPolygon | `[h:u16][item:u16][tipo:u8][n:u8][punti]`. | `0x009427E4`, invio `0x009428A0`. L'analisi verifica la richiesta e l'esistenza del percorso di risposta. Gli ACK di elementi ripetuti non sono una misura affidabile del numero di elementi resi. |
| `21B` ImageSetFillColor | `[h:u16][indice palette:u8]`. | Stringa `HUDTerminal.ImageSetFillColor` al literal di `0x00943394`, builder `0x009435EC`, setter `0x00CA1B70`, invio `0x0094365C`. Risposta verificata a livello di percorso generico, schema specifico del ricevitore F31 da accertare. |
| `220` ImageRemoveAllItems | `[h:u16]`. | `0x00941FB4` usa il nome del metodo e invia a `0x00942014` dopo `addSession(h)` senza setter aggiuntivi. Il backend usa lo svuotamento sul buffer prima di ridisegnarlo. |

Per la compatibilità dei riscontri HMI, `C01` HUDSelection è registrato a `0x02148D84` e legge il valore a 16 bit a `0x02148AA4`. `C02` AdjustHUD è registrato a `0x02148D94` e il parser di stato a `0x02148684` legge tre byte. Questi riferimenti sono in `HmiMain3D`. Nessuno dei due metodi è necessario per l'upload dimostrato. Nel codice progetto, la convalida dei payload e dei mittenti è in `milestone-7/src/cluster/hudterm.hpp` e `hud_terminal.cpp`.

## 4. C05 ImageAddBitmap

### Richiesta, byte per byte

**CONFERMATO DA CODICE ARM.** `0x00CA5594` costruisce tre parametri `u16`, quattro `u8`, uno stream e lo schema d'errore. L'ID è scritto a `0x00CA567C`. Il builder di segmento `0x0093D710` riceve `handle, x, y` secondo ABI ARM, poi `fileType, segId, segTotal, bitmapId, stream, dumpFlag` sullo stack. La trace `0x0101C6B8` nomina X/Y, formato, ID, totale e dimensione. I setter scrivono i parametri di chiamata nel seguente ordine. L'handle è preposto dalla serializzazione di sessione, `addSession` seguito da `sendAckEth` e `writeAckParams` nella libreria MOST.

| Offset payload | Lunghezza | Tipo | Campo | Prova nel builder ARM |
| ---: | ---: | --- | --- | --- |
| 0 | 2 | `u16be` | Handle della superficie | `0x0093D868 mov r1,r4` → `0x0093D870 addSession`. `r4` torna come handle a `0x0093D908`. |
| 2 | 2 | `u16be` | X | `0x0093D878 mov r2,r9` → setter `0x0093D880`, parametro 0. |
| 4 | 2 | `u16be` | Y | `0x0093D888 mov r2,r11` → setter `0x0093D890`, parametro 1. |
| 6 | 1 | `u8` | File type | `0x0093D898 ldr r2,[sp,#0xe0]` → setter `0x0093D8A0`, parametro 2. |
| 7 | 1 | `u8` | ID segmento | `0x0093D8A8 mov r2,r7` → setter `0x0093D8B0`, parametro 3. |
| 8 | 1 | `u8` | Numero totale di segmenti | `0x0093D8B8 ldr r2,[sp,#0x2c]` → setter `0x0093D8C0`, parametro 4. |
| 9 | 1 | `u8` | ID bitmap | `0x0093D8C8 mov r2,r10` → setter `0x0093D8D0`, parametro 5. |
| 10 | Variabile | Stream grezzo | Byte del segmento | `0x0093D8DC mov r2,r6` → setter `0x0093D8E0`, parametro 6. `CHBParaStream::writeToStream` MOST `0x0011AFC0` scrive direttamente i byte, senza lunghezza interna aggiuntiva. |

A `0x0093D90C mov r2,#6` seguito da `0x0093D910 bl sendAckEth` si verifica l'opcode effettivo. L'offset è relativo al payload HUDTerminal e parte dopo l'header MOST/DSI.

### Valori effettivi e segmentazione

```asm
0093EC34  cmp  r6, #1
0093EC38  bne  0x0093EC84
; Qui r6 == 1
0093EC5C  str  lr, [sp, #0x00]  ; fileType
0093EC6C  str  r6, [sp, #0x04]  ; segId = 1
0093EC70  str  r6, [sp, #0x08]  ; segTotal = 1
0093EC74  bl   0x0093D710
```

Il ramo Road Icon passa `1` come tipo bitmap sulla chiamata a `0x0077BE80` con `mov r12,#1` a `0x0077BE74` e store a `0x0077BE7C`. Per **Road Icon formato 1 e singolo segmento** il controller ARM produce quindi `01 01 01 <bitmapId>` agli offset 6–9. Il builder di segmento può ricevere altri tipi di file. Il formato `01 01 00 <bitmapId>` è osservato nella costruzione del backend e descritto per il percorso x86. Il ramo ARM qui analizzato usa un totale calcolato e, quando vale 1, serializza 1.

```text
upload(request):
    rifiuta se la stessa coppia istanza/bitmapId è già pendente
    stream = buffer ricevuto
    se fileType > 0x7f:
        stream = width_BE16 || height_BE16 || stream
    segmentSize = valore effettivo da configurazione/richiesta
    per buffer non vuoto:
        total = 1 se segmentSize == 0, altrimenti ceil(len(stream) / segmentSize)
    se total > 254: termina con errore locale
    registra upload pendente e numero di segmenti
    se total == 1: invia(h, x, y, fileType, 1, 1, bitmapId, stream)
    altrimenti: invia le porzioni in ordine, con segId a partire da 1
```

Riferimenti in `0x0093E928`:

- `0x0093EA34` controlla la coppia pendente, con log a `0x0101CA86`.
- `0x0093EAE4` confronta il tipo con `0x7F`. `0x0093EAF8/0x0093EB04` chiamano il writer `0x00CD6E74`, che scrive prima il byte alto e poi il basso, quindi aggiungono i due campi dimensione in testa allo stream.
- `0x0093EB68` legge la dimensione del segmento come `u16` dal gestore `+0x2C`. `0x0093EB78–0x0093EBA4` calcola e limita il totale, con ramo a un segmento quando la dimensione configurata vale zero. `0x0093EC30` inserisce lo stato pendente. `0x0093EC90` inizializza il numero di segmento a 1 e `0x0093ECBC/0x0093ECF4` leggono un blocco e invocano il builder.
- `0x00CA5638` crea `CHBParaStream(60000)`. Sono 60.000 byte per parametro stream del metodo. Il limite effettivo di una singola trasmissione dipende anche dal taglio, dal tipo TX, dalla libreria e dal ricevitore. Il controller applicativo segmenta il buffer anche se il livello di trasporto ha un proprio meccanismo di riassemblaggio.

### Risposte e concorrenza

**CONFERMATO DA CODICE ARM.** Il ramo `ResultAck` riceve l'handle e legge due valori `u8` con i getter `0x00CA08E8` (parametro di risultato 0) e `0x00CA0898` (parametro 1) chiamati a `0x0093DEA4/0x0093DEB4`. Il formato di trace a `0x0101C861` identifica esplicitamente `sessionHandle`, `bmpId` e `segmentId`. Letto come payload applicativo, il risultato è `[handle:u16be][bitmapId:u8][segId:u8]`. Il codice usa la coppia handle/bitmap per risalire allo stato dell'upload. Il callback superiore `0x0077F330` tratta l'esito e ha un percorso verso la commutazione. La condizione `m.data[3] == 1` del backend è quindi plausibile per un singolo segmento. Per una trasmissione segmentata va correlato il **numero del segmento**, evitando di trattarlo come flag generico.

Il ramo di errore a `0x0093DFF8/0x0093E00C` recupera il contesto della sessione. La stringa a `0x0101C902` riporta ragione, sessione, bitmap e segmento. Il costruttore ha il gruppo generico `u8` e `Stream(10)` a `0x00CA5644–0x00CA565C`. Il formato completo del dettaglio su filo per ogni codice d'errore e la presenza di bitmap/segmento in ogni `ErrorAck` della F31 restano aperti. Conservare i dettagli grezzi e correlare tramite peer, funzione, handle e contesto disponibile. L'accettazione locale di `sendAckEth` e l'esito del KOMBI sono eventi distinti. I pending ARM, gli ACK per segmento e i callback di esito dimostrano il comportamento asincrono. Il TX effettivo OEM ARM di questa configurazione non è ricavato da questi soli builder.

| Aspetto | OEM ARM | Backend corrente | Fonte esterna x86 |
| --- | --- | --- | --- |
| Ordine campi | h, x, y, fileType, segId, segTotal, bitmapId, stream | Identico ordine | Identico schema numerico riportato |
| Road Icon a segmento singolo | `01 01 01 id` | `01 01 00 id` | `01 01 00 id` nel test pubblicato |
| Dimensione | Stream dichiarato 60.000 byte e gestione multipart | Intera immagine, tetto complessivo 4096 byte, validatore vincolato a totale 0 | Dimensione utile dipendente da TX e superficie, benchmark separati TX0/TX1 |
| ACK | `h, bitmapId, segId` | Accetta quattro byte con ID corretto e ultimo byte `1` | Risposta a segmento singolo con ultimo byte `1` osservata |
| Swap | Percorso di completamento della bitmap prima della visibilità nel ramo analizzato | Upload prima di `20E` nella coda locale, ACK monitorato separatamente | Benchmark e comportamento di un'altra piattaforma |

**Indicazione implementativa.** Scrivere fixture indipendenti per le due forme. Fare accettare al validatore la forma esplicita del profilo, con `segId=1, segTotal=1` per una modalità OEM ARM. Conservare la forma legacy nel profilo corrente fino alla verifica sul 6WB. Prima di abilitare l'upload multipart, provare gestione dei risultati per segmento, scadenze, duplicati e identificativi riutilizzati. Uno swap dipendente da un bitmap nuovo va rilasciato alla conferma dell'upload richiesto. Riscontri sorgente `milestone-7/src/cluster/hud6wb_codec.cpp` (`bitmap`), `hudterm.hpp` (`payloadShapeOk`), `hud_terminal.cpp` (`graphicPayload`, `Core::onGraphicAnswer` e matcher delle chiamate in-flight). [Misure e formato JayJay x86](https://github.com/JayJay3071/nbt-android-auto/blob/main/docs/CLUSTER-HUDTERMINAL.md#bitmaps-flächengrößen-und-spurzeile-auto-2026-11-08)

## 5. Primitive vettoriali 21D e attributi

```text
+0  u16be  handle
+2  u16be  item index
+4  u8     tipo (2 polilinea, 3 poligono)
+5  u8     numero di punti
+6  stream  n × (x:i16be, y:i16be) nel formato usato dal backend
```

**CONFERMATO DA CODICE ARM.** `0x009427E4` mette in sessione l'handle e imposta item, tipo, conteggio e stream, in quest'ordine. `0x0077C3A8` e `0x0077C280` invocano `0x00778E64` con tipi 2 e 3. Il costruttore `0x00CA5CD4` registra `21D` a `0x00CA5D7C–0x00CA5D84`. Il count è un byte con metadati `0x01127548`, inizializzati a minimo 1 e massimo 100 a `0x00CA996C–0x00CA9978`. `CHBParaNumberInfo` in MOST `0x00119D00` scrive minimo e massimo. Lo stream ha capienza 400 a `0x00CA5D44`. `0x00778EF8` compara con 100 e il ramo superiore avvisa prima di proseguire. Il builder inoltra lo stream dei punti. La codifica a coppie signed big-endian è quella del builder di progetto `hudterm::addPolygon`, coerente con 400 byte per 100 punti e con le implementazioni vettoriali di riferimento. Nella traccia ARM del solo builder, le coordinate sono già nello stream.

**CODICE PROGETTO.** `milestone-7/src/cluster/hudterm.hpp` impone 99 punti. `hud6wb_kit.cpp` usa forme entro tale capienza o scompone la geometria del kit in più item. Un punto di chiusura esplicito, se emesso, rientra nel conteggio. Per spezzare una polilinea con continuità si può condividere il vertice al confine. Per un poligono riempito serve una suddivisione geometrica che conservi contorno, ordine e fori. Il builder ARM non dimostra che il KOMBI concateni automaticamente i punti fra chiamate successive. **DA VERIFICARE SUL VEICOLO:** accettazione del vertice 100, dimensioni utilizzabili, clipping e coordinate prospettiche. L'attribuzione del limite 99 al F010 KOMBI-L6 è una nota del progetto, senza decoder F31 disponibile per confermarla.

Gli attributi di linea e riempimento sono operazioni separate. Il progetto usa `218` colore linea, `219` larghezza, `21B` colore riempimento, `21C` modalità, ciascuno con handle e un byte. `21B` è un indice di stile/palette. Per un frame riproducibile, inviare gli attributi necessari prima di aggiungere gli item. Le funzioni `setLineColor`, `setLineWidth`, `setFillColor`, `setFillMode`, `addPolygon` e `removeAllItems` sono in `milestone-7/src/cluster/hudterm.hpp`. Per palette e geometria, vedere `hud6wb_kit.cpp`, `grafite_oro/hudterminal_palette.cpp`, `milestone-7/docs/6WB_VECTOR_COORDINATES.md` e `6WB_PALETTE_CALIBRATION.md`.

## 6. Bitmap e fedeltà cromatica

**CONFERMATO DA CODICE ARM.** Sono presenti selezione e registrazione dei compressori `NBT_RLE32` e `NBT_RLE16` e un percorso di inoltro bitmap già compressi. La scelta dipende dal ramo del controller e dalle capacità/configurazione. Il Road Icon analizzato usa il tipo file 1. Il codec RLE16, il packing dei suoi pixel e il suo uso effettivo sulla F31 restano aperti. L'algoritmo di compressione ARM, l'ordine dei canali e la semantica completa dell'alpha sul decoder F31 non sono verificati con un campione ARM.

**FONTE ESTERNA e CODICE PROGETTO.** JayJay identifica `CGVRLE32Compressor` in `NavigationGuidanceViewer_NV` x86, VA x86 `0x080F0972`. Il nome è una pista per il produttore ARM, di cui mancano file e percorso verificati. Il codec di progetto `hud6wb_codec.cpp::rle32` implementa questa forma:

- Flusso di pixel in ordine di riga, senza separatore fra righe.
- Pacchetto ripetuto `[0x80 | n][A B G R]` per 2–127 ripetizioni. Pacchetto letterale `[n][n × (A B G R)]` per 1–127 pixel.
- Padding con zeri fino a un multiplo di otto, aggiungendo otto byte quando la lunghezza è già allineata.
- Il riferimento x86 riporta trasparenza per alpha zero. La composizione di alpha intermedio sulla F31 va verificata. Il backend può preparare bordi con copertura precomposta, coerentemente con il fondo verificato del profilo.
- I test x86 distinguono risposta positiva e immagine visibile. Per tipo 1, il profilo corretto richiede le dimensioni adatte all'istanza. Il codice ARM ha un prefisso dimensioni per tipi superiori a `0x7F`. Non è stato verificato un tipo superiore a `0x7F` accettato dalla F31.

Grafite, bianco e oro sono colori **del kit**. Il colore vettoriale reso dipende dall'indice, dalla modalità visuale e dal quadro. Gli indici 0–19 provengono dai riferimenti x86 e dalle calibrazioni di progetto. Il backend può mantenere geometria e scelta semantica del colore, e può tradurla in valori per pixel su una superficie bitmap verificata. Manca la prova di fedeltà RGB e alpha per tutte le superfici F31. [Riscontri di compressione, palette e visibilità x86](https://github.com/JayJay3071/nbt-android-auto/blob/main/docs/CLUSTER-HUDTERMINAL.md)

## 7. Superfici e composizione

**CONFERMATO DA CODICE ARM.** Il frame è una composizione di oggetti nominati. Il risolutore `0x0077A5A4` sceglie un nome da un indice software. Il record usato da `0x007731AC` ha stride 24. Il reale handle è letto come `u16` dal record, per esempio a `0x00778F84`. I selettori A/B usano stato di visibilità e indici, tra cui i letterali `0x18/0x19` a `0x0077AF64`. Quei letterali non assegnano un nome agli handle MOST numericamente uguali.

| Nomi ARM verificati | Posizione nell'ELF del controller |
| --- | --- |
| `Top_Frame_11/12/13`, `Coming_Road_11_1`, `Distance_11_1`, `Unit_11_1` | Pool da `0x010C0CF1`. Nomi analoghi per 12 e 13. |
| `Road_Icon_11_1/2`, `Road_Icon_12_1/2`, `Road_Icon_13_1/2` | `0x010C0D27/0x010C0D36`, `0x010C0E3B/0x010C0E4A`, `0x010C0ED5/0x010C0EE4`. |
| `Next_Maneuver_Arrow_11_1/2`, `Next_Maneuver_Arrow_13_1/2` | `0x010C0D45/0x010C0D5E`, `0x010C0EF3/0x010C0F0C`. |
| `Guiding_Icon_11_1/2`, `Guiding_Arrow_11_1/2`, `Lane_Guiding_11_1/2` | Pool `0x010C0D77–0x010C0DD3`, con coppie analoghe per il frame 13 nel pool `0x010C0F25–0x010C0F81`. |
| `Junction_View_12_1/2`, `Bargraph_11_1`, CCP e Micro Map | Junction View `0x010C0E59/0x010C0E6C`, Bargraph 11 `0x010C0DF7`, altri nomi nello stesso pool `0x010C0DE5–0x010C101A`. |

**INFERITO.** L'istanza `Next_Maneuver_Arrow` è la candidata per la piccola freccia sotto il nome strada. La conferma richiede il nome restituito/risolto, l'handle e l'associazione visiva durante l'aggiornamento. Un ACK `C04` verifica l'esistenza dell'handle e, quando ha i byte completi, le dimensioni. Non contiene il nome.

| Handle MOST | Ruolo nel profilo attuale o da fonte esterna | Dimensioni e riscontro |
| --- | --- | --- |
| `0x17` | Contenitore navigazione | **OSSERVATO F31** 309 × 330, risultato completo `00 17 01 35 01 4A`. |
| `0x18/0x19` | Buffer A/B del bitmap superiore e freccia successiva personalizzata | 305 × 44 nel profilo x86. Il CSV F31 registra risultati `C04` a sei byte per entrambi senza il corpo integrale, dunque da lì non si ricavano larghezza e altezza. |
| `0x1A`, `0x1B`, `0x1C` | Strada, distanza numerica, unità | Assegnazioni del profilo, in linea con i riferimenti x86. Nessuna dimensione F31 nel CSV per questi campi testo. |
| `0x1F/0x20` | A/B manovra centrale | Profilo `C04` 221 × 158, spazio vettoriale sperimentale 233 × 206. **OSSERVATO F31** due ACK a sei byte. Dimensioni del profilo e coordinate di input sono dati distinti. |
| `0x23/0x24` | A/B della riga corsie | 283 × 40 da profilo/riferimento x86. **OSSERVATO F31** due ACK a sei byte. Compatibilità per primitive e bordi da calibrare su questa revisione. |
| `0x35` | Barra laterale della distanza | 28 × 113 da profilo/riferimento x86. **OSSERVATO F31** ACK a sei byte. Il profilo aggiorna il livello tramite `227`. |
| `0x1D/0x1E`, `0x21/0x22`, `0x25` | Superfici secondarie, striscia e piano prospettico, Carsor | **FONTE ESTERNA x86** 145 × 44 per `1D/1E`; 221 × 158 per `21/22/25`. Manca riscontro F31 nel materiale consolidato. |

La documentazione x86 mantiene anche un riferimento precedente a 49 × 44 per `0x19` e descrive successivamente 305 × 44 e la prova con immagine 49 × 44 visivamente errata. Usare la dimensione restituita e validata per l'istanza della revisione in esame. Riferimenti del progetto `milestone-7/src/cluster/hud6wb_profile.hpp`, `hud6wb_resources.hpp`, `hud6wb_display.cpp` e `milestone-7/nbt-aad-6wb-diag.csv` righe 20751, 21418, 22125, 22936, 23659, 24346, 24995, 25676 per gli otto risultati di fase 6. [Riscontri di superficie x86](https://github.com/JayJay3071/nbt-android-auto/blob/main/docs/CLUSTER-HUDTERMINAL.md)

## 8. Sincronizzazione, errori e tempi

La sequenza di riferimento per il backend è validare risorse e peer → costruire sul buffer nascosto → inviare attributi e item → completare upload bitmap richiesto → commutare con `20E` → aggiornare la visibilità del contenitore secondo il frame → pulire il buffer quando occorre riusarlo. A fine guida nascondere le istanze, svuotare gli item e cancellare i testi secondo l'attuale ciclo di vita del backend.

- **ARM.** `0x0093EC30` aggiunge l'upload alla coda dei pendenti. Nel gestore risultato, `0x0093DF10–0x0093DF2C` aggiorna il conteggio degli esiti e confronta con il totale previsto. I rami `0x0093DF6C/0x0093DF84` segnalano esito positivo o errore a livello superiore. Questo schema prova l'attesa del completamento applicativo per l'upload. La politica di deduplicazione per segmento non è derivabile dal solo ramo del contatore. Il backend può deduplicare esplicitamente attraverso un insieme di segmenti confermati.
- **CODICE PROGETTO.** Il pacing grafico è nominalmente 15 ms nel resoconto del backend con ledger asincrono. `Core::rememberGraphic`, `Core::expireGraphics`, `graphicPayload`, `Core::onGraphicAnswer` e l'invio via DSI si trovano in `milestone-7/src/cluster/hud_terminal.cpp`. Le risposte tardive, duplicate e ambigue vanno associate senza attribuirle a una generazione nuova usando solo il tag. Il progetto non può dedurre completamento del disegno dal completamento del dispatch locale.
- **C04.** Timeout diagnostico 3000 ms, ultimo termine del profilo sperimentale 5000 ms. Il client evita di sovrapporre una seconda richiesta indistinguibile per stesso peer/tag/istanza. La cache valida è associata al collegamento/host. Nel CSV F31, gli intervalli sono circa 3,35–3,40 s dall'invio applicativo osservato alla notifica DSI. In particolare, per `0x17`, 2847549152448 → 2850918149079 ns equivale a circa 3369 ms. Sono misure end-to-end, non tempo del solo decoder.
- **FONTE ESTERNA x86.** JayJay riferisce qualche centinaio di millisecondi per immagini piccole e differenze fra TX0 e TX1 per stream più grandi. Si tratta di un riferimento operativo distinto dalla F31 ARM. Il progetto corrente seleziona TX4 per vettori e TX0 per bitmap. Prima di modificare TX o dimensione dei segmenti serve una fixture coerente con il profilo o una cattura del corrispondente percorso OEM. [Benchmark x86](https://github.com/JayJay3071/nbt-android-auto/blob/main/docs/CLUSTER-HUDTERMINAL.md)

## 9. Android Auto e tabella OEM

**CODICE PROGETTO.** Canale AA 10, ID messaggio iniziale a 16 bit big-endian e corpo protobuf documentato in `milestone-7/proto/aa_nav.proto`. Il parser è `viewFromMessages` in `milestone-7/src/cluster/nav_msg.hpp`. La macchina a stati `nav_core.cpp` correla stato, distanza, ordine dei dati e fine percorso.

| ID | Campi utilizzati |
| --- | --- |
| `8003` | Stato guida attiva, inattiva o ricalcolo. |
| `8006` | `steps[]` e destinazioni. Il primo passo contiene `maneuver(type, exit_number, exit_angle)`, strada, corsie e possibili cue. Per il secondo passo il codice conserva tipo e angolo, mentre il numero di uscita del secondo passo è disponibile nello schema e non viene conservato da `NavView`. |
| `8007` | Distanza al passo in metri, testo e unità, secondi, eventuale distanza alla destinazione e strada corrente. |
| `8004/8005` | Formati legacy. Il parser traduce `event/turn_side` e legge i dati di uscita quando presenti. La posizione legacy espone distanza, tempo e valore visualizzato scalato. |
| Corsie | Lista ordinata da sinistra a destra. Una corsia può avere più direzioni, ognuna con `shape` e `is_highlighted`. Il renderer 6WB usa il kit grafico. La distinta funzione `laneArrow` contiene un mapping per un altro percorso KOMBI e ha alcune voci segnate nel sorgente come inferite. |

`GrafiteKit::maneuver` in `hud6wb_kit.cpp` è il mapping operativo AA 0–46. Copre sconosciuta, inizio, cambio strada, svolte di diverse intensità, inversioni, rampe, uscite, biforcazioni, immissioni, rotatorie in entrambi i sensi, prosecuzione, traghetti e destinazione. I log AA Google Maps e Waze nell'albero `LOG` sono materiale per replay locale. Il rapporto usa solo schema e semantica dei campi.

**CONFERMATO DA CODICE ARM.** La jump table a `0x00D47088` in `0x00D47068` fornisce i nomi del campo `maneuverDirection`, chiamata da `0x00D47D0C` nel trattamento descrizione manovra. Il parser raggiunge il gestore a `0x00D504E0`. Questi valori non vengono serializzati da questo percorso come primitive HUDTerminal.

| Codici hex | Nomi in ordine |
| --- | --- |
| `00–09` | `NO_DIRECTION`, `STRAIGHT_ON`, `HALF_LEFT`, `HALF_RIGHT`, `TIGHT_LEFT`, `TIGHT_RIGHT`, `LEFT`, `RIGHT`, `UTURN_LEFT`, `UTURN_RIGHT` |
| `0A–0D` | `DRA_TARGET_POINT_CENTER/LEFT/RIGHT`, `DRA_NOT_DIGITISED` |
| `0E–17` | `ROUNDABOUT_CW/CCW`, `ROTARY_0/45/90/135/180/225/270/315` |
| `18–1F` | `HIGHWAY_IN_STRAIGHT/LEFT/RIGHT`, `HIGHWAY_OUT_STRAIGHT/LEFT/RIGHT`, `ENTER_HIGHLEVEL_ROAD`, `ENTER_UNDERPASS_ROAD` |
| `20–29` | `LANE_CHANGE_LEFT/RIGHT`, `TUNNEL`, `BRANCH_LEFT/RIGHT`, `FORK_LEFT/RIGHT`, `FORK_TRIPPLE_LEFT/MIDDLE/RIGHT` |
| `2A–33` | `EXIT_LEFT_ECE`, `EXIT_RIGHT_ECE`, `ROTARY_0/45/90/135/180/225/270/315_ASIA` |
| `34–37` | `JUNCTION_ASIA`, `INTERSECTION_ASIA`, `PARKING_AREA_ASIA`, `SERVICE_AREA_ASIA` |

`LGRoundAbout` ha anche senso, indice di entrata, indice di uscita e lista di strade (`0x00D4FBC0`, trace `0x010C3CCE`). `LGBranch` e `LGIntersection` includono campi separati per tipo di incrocio o diramazione e indice/direzione d'uscita. Il logger OEM non stabilisce una corrispondenza fra questo vocabolario e quello AA. Lo stesso valore numerico può significare cose diverse. Per tipo sconosciuto o dati incompleti, mantenere le regole esplicite del kit e la possibilità di mostrare distanza e nome separatamente. Per le rotatorie conservare presenza del numero di uscita e dell'angolo, senza inventare un numero quando manca.

## 10. Lacune e criteri per chiuderle

1. Configurazioni attestate dalla riga di avvio `/opt/nav/eceusa/bin/navigation.ini` e `/mnt/share/nav/NavCtrl_DB.ini`, assenti dai reperti. Servono per identificare binding, privilegi, stili e produttore.
2. Writer del displayable `GV_ICHUD` dietro ManeuverView, schema d'ingresso e risorse grafiche della build ARM. Gli identificatori `L7HUDFPKGVDay` e `L7HUDFPKGVNight` sono presenti in ARM a `0x010C10F6/0x010C1104`. `NavigationGuidanceViewer_NV` è il nome del reperto **x86** pubblicato, candidato di ricerca per una controparte ARM. Percorso, nome esatto e dipendenze del produttore ARM rimangono aperti.
3. Traccia OEM F31 con risoluzione nome-handle per `Next_Maneuver_Arrow` e `Road_Icon`, e una transizione con dati completi `21D/C05/20E`. L'associazione allo schermo richiede un riscontro sincronizzato. Servono risultati ed eventuali errori per confrontare i flag, il TX e la segmentazione effettiva.
4. Firmware KOMBI della revisione installata o riscontri specifici sul decoder per punto 100, modalità di fill, ritaglio prospettico, alpha e bitmap multipart. Separare accettazione del metodo da presentazione del contenuto.
5. Le note sul proxy `52/00` non apportano un collegamento provato al produttore delle primitive. Il percorso diretto verso HUDTerminal ha supporto indipendente. [Contesto esterno dell'integrazione MOST su MINI F60](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/issues/52)

## IMPLEMENTATION_HANDOFF

1. **Punto di partenza.** Usare `NavMessages → viewFromMessages → NavView → Hud6wbDisplay`. Mantenere l'invio attraverso `HudTerminal` e `most::Client`. Non serve incorporare il controller o i file OEM nel prodotto. La riproduzione fattibile riguarda i builder e il ciclo di completamento `0x0093E928 → 0x0093D710`, la preparazione di `21D` `0x00778E64 → 0x009427E4` e lo swap `0x00945668`.
2. **Strutture.** Aggiungere al modello implementativo una descrizione della coppia di superfici con ruolo, handle, dimensioni e profilo di provenienza, una vista di due passi AA con campi opzionali di rotatoria, e un record upload con peer/istanza/FID/tag, handle, bitmap ID, totale, segmenti confermati, generazione e scadenza. Evitare di dedurre la generazione da un tag di routing riusato. Conservare i ruoli OEM separati da handle e dall'enum AA.
3. **Builder e validazione.** In `milestone-7/src/cluster/hud6wb_codec.cpp` e `hudterm.hpp` modellare i campi `C05` della sezione 4. Il validatore corrente rifiuta `payload[8] != 0`. Usare fixture separate per totale OEM `1` e profilo legacy `0`. Correlare `[h, bitmapId, segId]` in `hud_terminal.cpp::graphicPayload` e nel matcher delle risposte in-flight, mantenendo il controllo del peer e del tag. Conservare `C04` a sei byte per il profilo F31, con richiesta compatibile col riscontro già registrato. Il punto 100 e TX diversi restano opzioni da validare.
4. **Sequenza.** Nelle funzioni di costruzione batch di `hud6wb_display.cpp`, tenere nascosto il buffer di destinazione fino al completamento applicativo dell'upload da cui dipende la commutazione. Duplicato tardivo, errore o timeout devono chiudere quella generazione senza avanzare la superficie attiva per errore. Riutilizzare pacing e controllo dell'attività OEM del terminale. Verificare righe multiple della coda, aggiornamenti rapidi e fine guida.
5. **Superficie dedicata.** Collegare il rendering del secondo passo ad una coppia `Next_Maneuver_Arrow` solo con associazione verificata sulla revisione del profilo. `C04` protegge dalle dimensioni inattese, mentre risoluzione nome-handle e riscontro visivo identificano il ruolo. Per rotatoria, conservare nel secondo passo anche l'eventuale numero di uscita.
6. **Fallback e decisione OEM.** Il servizio produttore OEM non ha ancora un'API di iniezione verificata. Usare il renderer personalizzato `GrafiteKit`, i vettori per il corpo principale/corsie e il fallback bitmap del profilo verificato per la piccola freccia. Dove la risorsa è sconosciuta o rifiuta l'upload, lasciare attivi i livelli di guida compatibili ed evitare di produrre un glifo su una superficie dall'identità incerta. Palette e alpha vanno governati dal profilo, con i significati grafite/bianco/oro mantenuti nel kit.
7. **Verifica offline prima della prova veicolo.** Estendere `milestone-7/tests/test_hud6wb_async.cpp`, `test_hud6wb.cpp`, `test_hudterm.cpp` e `test_kombi_nav_msg.cpp` con caso singolo/segmentato, risposte in ordine diverso, duplicato, errore, ACK tardivo, cancellazione e dati guida Google Maps/Waze incompleti. I casi da verificare sul veicolo sono l'assegnazione delle istanze, la compatibilità dei due totali `C05`, il TX, il limite ricevitore `21D`, la visibilità e la resa cromatica. Nessuna di queste prove è stata eseguita durante l'esportazione.
