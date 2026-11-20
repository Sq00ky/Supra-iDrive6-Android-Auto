# Renderer destinazione Work e integrazione Sq00ky 4b378153

Intervento autorizzato il 18 novembre 2026 sul branch `feature/bmw-6wb`. Nessuna compilazione, esecuzione del daemon, installazione o push.

## Renderer aggiornato prima del checkpoint

Archivio fornito: `C:/Users/memel/Downloads/6wb-renderer-final.zip`.
SHA-256: `ff20dbcfeaed85b72df7a52718ac071d45eb5c28896b689465e99c9c031feae7`.

Il confronto con la working tree ha individuato due aggiornamenti grafici effettivi:

- `src/cluster/grafite_oro/artwork6wb.cpp`: nuova `destinationArtwork()` per le destinazioni 39-42, con bandiera a sinistra e strada/freccia a destra. La selezione avviene soltanto per la manovra centrale (`!miniature`); il percorso delle miniature resta invariato.
- `src/cluster/hud6wb_kit.cpp`: ruolo nero associato all'indice BMW 9, esattamente come nello ZIP. Nel catalogo precedente il ruolo Black era utilizzato soltanto dalle quattro destinazioni; le altre manovre non cambiano palette.

Percorso verificato staticamente: tipo Android Auto 39-42 -> `GrafiteKit::maneuver()` -> `GrafiteKit::central()` -> `renderManeuver()` -> ramo `destinationArtwork()` -> fit ed encoding sul buffer centrale 0x1F/0x20. Nessun fallback al vecchio motore.

Le copie infrastrutturali precedenti presenti nello ZIP non sono state sovrascritte sui sorgenti locali: preservati AndroidX 47-50, decoder/diagnostica AmiGO, accessor che corregge `cfg_`, superfici statiche, discovery disattivata e generatore cartelli attuale. Nessun vecchio generatore A/B reintrodotto.

Il manifest Work e aggiornato alla nuova revisione, conservando gli hash della precedente. L'overlay AndroidX continua a distinguere l'adattamento del parser dagli algoritmi grafici autorevoli. Aggiunto un controllo statico del nuovo percorso destinazione.

## Protezione del lavoro locale

- HEAD iniziale: `473b393e2ad6894d54b8b284bfa07c8cdc7ec52b`.
- Riferimento locale: `safety/pre-sq00ky-20261118`, sullo stesso HEAD iniziale.
- Checkpoint: `d5f8e520`, messaggio `Checkpoint local 6WB Work destination, AmiGO diagnostics and AndroidX support`.
- 52 percorsi di progetto inclusi nel checkpoint: 37 modifiche/eliminazioni gia presenti e 15 file pertinenti non tracciati. Le precedenti modifiche non committate sono ora conservate nel checkpoint locale, come autorizzato.

Backup esterni alla cronologia, nella cartella `C:/Users/memel/BMW-6WB/output/integration-safety-20261118/`:

- `pre-destination-sources.zip`: byte precedenti dei due sorgenti aggiornati e del manifest.
- `local-before-merge.zip`: copie della working tree pertinente, patch binaria Git, patch dell'indice, copia dell'indice, stato e manifest con SHA-256; archivio e contenuto verificati.
- SHA-256 di `local-before-merge.zip`: `245150176b15a77802558f2ef379050e8dfe937caf143af7081a38fd63e071e8`.
- `manifest.json`, `checkpoint-paths.json`: elenco esatto e hash dei file protetti.
- `j29-golden-before-lf.bin`: byte Windows precedenti alla normalizzazione del solo fixture J29 prevista dalla nuova `.gitattributes` upstream.
- `post-merge-static-tests.log`: risultati completi dei controlli Python/statici.

Log, acquisizioni JSONL, binari QNX, file OEM, dump e materiale di reverse engineering non sono stati inclusi nei commit, cancellati o sovrascritti. I backup restano non tracciati nella cartella output.

## Integrazione upstream

Target esatto: `4b378153d3a4d0881c503e7ac6f0285699cb7bb7`, gia disponibile come `upstream/main`. Antenato comune: `441b389c422092028aa13af6a68abad973aefa2b`. Nessun aggiornamento a una revisione diversa da quella richiesta.

Merge eseguito dopo il checkpoint con `--no-commit --no-ff`. **Nessun conflitto testuale.** Git ha integrato automaticamente CMakeLists, Makefile, readme, main.cpp e tests/Makefile; il risultato e stato controllato prima del commit locale finale.

I 14 commit esclusivi integrati, inclusi i merge, sono:

```text
cf3c538a  nbtpkg: tre percorsi log RAM
0012e65f  fixture J29 con terminazioni LF su Windows
526d966d  impostazioni nbt-aad.env nel manifest nbtpkg
91c9dd08  localizzazione tedesca
9fdf3af9  localizzazione tedesca e testo About
ee1ab1e8  merge PR #245
37c41828  merge PR #246
94be7c7d  correzioni reset iniziale USB e intervalli tra sessioni
daa3ae2c  password propria Wi-Fi Direct
26e2318f  selezione automatica solo senza WFDPass.conf utilizzabile
b13f3a0a  controllo riappropriazione gruppo e impostazione iDrive
86859f27  frequenza interrogazioni connection manager
9771a9fb  recupero SSID e diagnostica configurazione mancante
4b378153  merge PR #244
```

I 24 file upstream esaminati e integrati:

| Area | File |
|---|---|
| Git / CI | `.gitattributes`, `.github/workflows/nbtpkg.yml` |
| Build e documentazione | `milestone-7/CMakeLists.txt`, `milestone-7/Makefile`, `milestone-7/readme.md` |
| RHMI | `rhmiapp/NbtAad.xml`, `NbtAad_ALL_Texts.zip`, `make_db.py`, `nbt_aad_app.cpp` |
| Avvio e Wi-Fi Direct | `src/main.cpp`, nuovi `gcf_client.cpp/.hpp`, `wfd_own.cpp/.hpp` |
| USB | `src/usb_accessory.cpp`, `usb_aoap.hpp`, `usb_direct.cpp/.hpp` |
| Test | `tests/Makefile`, `test_usb_accessory.cpp`, nuovo `test_wfd_own.cpp` |
| Packaging | `tools/nbtpkg/README.md`, `manifest.json`, `test_make_nbtpkg.py` |

Il solo file aggiuntivo del commit di merge e questo rapporto. La normalizzazione LF del fixture J29 rende la working copy coerente con il blob Git e con l'attributo upstream, senza cambiare il contenuto del transcript.

## Compatibilita verificata

Dei 52 percorsi del checkpoint, il merge modifica soltanto `CMakeLists.txt`, `Makefile` e `tests/Makefile`, aggiungendo le sorgenti ufficiali. Tutti gli altri file locali protetti, inclusi renderer, adattatore AndroidX, superfici, parser, diagnostica, MOST/HUDTerminal e `aa_session.cpp`, conservano gli stessi byte del backup dopo l'aggiornamento destinazione.

Makefile e CMake elencano gli stessi 56 sorgenti senza duplicazioni o percorsi mancanti. Sono presenti `wfd_own.cpp` e `gcf_client.cpp`, insieme alle sorgenti USB e ai moduli 6WB gia necessari. La rimozione del vecchio `hud6wb_artwork.cpp` resta preservata. Conservati flag QNX, librerie, supporto USB e configurazione RPATH del fork.

`main.cpp` mantiene l'avvio della diagnostica 6WB e incorpora il nuovo flusso Wi-Fi Direct. L'header e le definizioni di `UsbDirect::noteSwitchSent()` sono coerenti nei percorsi QNX e host; la chiamata e presente prima dello switch accessorio. Gli intervalli di retry USB 5/10/20 secondi e i timeout del nuovo collegamento Wi-Fi sono aggiornamenti ufficiali del trasporto di connessione, non attese introdotte nel renderer o nella discovery 6WB.

La nuova funzionalita Wi-Fi Direct usa le librerie gia presenti sulla vettura sotto `/opt/conn/lib`, caricate dal processo figlio; non sono state copiate librerie o eseguite chiamate al connection manager. Il default `NBT_WFD_OWN_PASS=auto` conserva la password della vettura quando WFDPass.conf e utilizzabile. Nel manifest di packaging upstream e richiesto l'agent NBTApps `>1.4.1`: e un requisito del pacchetto, non una nuova toolchain installata.

## Verifiche eseguite e limiti

- 43 test statici pertinenti passati prima del checkpoint, dopo l'integrazione destinazione.
- **58 test Python/statici passati dopo il merge**, includendo renderer, regressioni su fixture Maps/Waze/AmiGO, contratti AndroidX, diagnostica JSONL e modelli di persistenza/rotazione.
- Verificate inclusioni locali e delimitatori lessicali nei sorgenti C++ modificati upstream; sintassi Python dei file modificati.
- Verificati tutti i percorsi e l'unicita delle sorgenti Make/CMake.
- ZIP delle dieci lingue RHMI verificato tramite CRC, senza rigenerare asset o binari.
- Manifest NBTApps verificato staticamente: tre percorsi log, dieci opzioni e requisito agent.
- Nessun conflitto residuo nell'indice e `git diff --check` / `git diff --cached --check` superati.

La suite Python `tools/nbtpkg/test_make_nbtpkg.py` e stata avviata ma il modulo e stato **saltato**, perche manca `C:/Users/memel/nbtapps/headunit/tools/nbtpkg.py`. Nessuna dipendenza installata per aggirare il limite.

I test C++ USB, Wi-Fi Direct, renderer e trasporto non sono stati eseguiti: richiedono compilazione, esclusa dall'utente. Non e stato eseguito alcun compilatore, make, CMake configure/build, programma QNX o deployment. Le verifiche statiche non dimostrano la compilazione QNX o il risultato fisico sul quadro; nuova destinazione e nuove funzioni di collegamento richiedono il successivo test manuale.

La procedura manuale `milestone-7/build-6wb.cmd` rimane invariata. Il commit di merge finale ha come genitori il checkpoint locale `d5f8e520` e l'upstream esatto `4b378153`; il relativo hash e riportato nella consegna finale e in `output/integration-safety-20261118/result.json`.
