# Integrazione del renderer Work 6WB

Data: 18 novembre 2026. Sorgenti preparati per la compilazione manuale; nessuna compilazione eseguita.

## Stato Git e provenienza

- Branch: `feature/bmw-6wb`.
- HEAD invariato: `473b393e2ad6894d54b8b284bfa07c8cdc7ec52b`.
- Tag verificato: `6wb-pre-renderer-work`, sullo stesso commit.
- Le modifiche sono nella working tree, senza commit o staging.
- Archivio: `C:/Users/memel/Downloads/6wb-renderer-final.zip`.
- SHA-256 archivio: `5402e8ecb3e7dce8a3979fca989dfee6ca5a939d801d50fe4989cc660078bdde`.
- `6wb-work-renderer-manifest.json` registra gli hash di 12 file sorgente/dati confrontati direttamente con lo ZIP. L'unica normalizzazione riguarda CR/LF; geometrie, colori e algoritmi sono identici al pacchetto.

Le istruzioni di esportazione originali sono conservate in `src/cluster/grafite_oro/INTEGRAZIONE-CODEX.md`. I test dichiarati in quel documento sono quelli dell'autore dell'export: non sono stati rieseguiti qui, perche richiedono compilazione. Non riapplicare il patch dello ZIP dopo questa integrazione.

## Modifiche ai sorgenti

| File sotto `milestone-7/` | Risultato |
|---|---|
| `src/cluster/grafite_oro/artwork6wb.cpp`, `.hpp` | Motore definitivo Work per manovre, rotatorie, corsie e miniature |
| `src/cluster/grafite_oro/hudterminal_palette.cpp`, `.hpp` | Ruoli Work e palette BMW a 20 indici, incluso `ExitWhite` |
| `src/cluster/grafite_oro/miniature_raster.hpp` | Nuovo raster Work per miniature bianche |
| `src/cluster/hud6wb_road_sign.hpp` | Nuovo generatore originale Work A/SS/SP, header-only |
| `src/cluster/hud6wb_kit.cpp` | Adattatore originale del pacchetto e palette dei piani |
| `src/cluster/hud6wb_display.cpp` | Composizione originale Work di bitmap, miniature e testo |
| `src/cluster/hud6wb_profile.hpp`, `hud6wb_config.hpp` | Dimensioni misurate e parametri cartelli del pacchetto |
| `src/cluster/hud6wb_codec.cpp`, `.hpp` | Rimossi soltanto il vecchio riconoscimento A/B e il relativo disegno; codec testo, RLE32 e C05 preservati |
| `src/cluster/hud6wb_artwork.cpp`, `.hpp` | Eliminato il generatore precedente, ormai privo di chiamanti |
| `Makefile`, `CMakeLists.txt`, `tests/Makefile` | Rimossa la vecchia unita di compilazione |
| `tests/test_hud6wb.cpp`, `test_hud6wb_kit.cpp`, `hud6wb_kit_test_util.hpp` | Test C++ aggiornati alle API/layout Work; aggiunti casi cartelli e cancellazione, preparati ma non eseguiti |
| `tests/test_hud6wb*_static.py`, `test_hud6wb_flat_geometry.py` | Contratti statici aggiornati; eliminato il precedente modello Python di disegno parallelo |
| `tools/preview_6wb.py` | Riferimento al vecchio generatore rimosso; colori letti dagli helper Work anziche dalla vecchia palette locale; anteprima non generata |

`artwork_data.inc` e `maneuvers.inc` erano gia identici allo ZIP: verificati e inclusi nel manifest, senza modifiche superflue. Il riferimento JSON delle tabelle rimane coerente con questi dati autorevoli.

Non esiste un fallback al renderer precedente. La gestione recuperabile delle geometrie non rappresentabili mantiene il testo e nasconde le superfici interessate: non genera simboli alternativi.

## Collegamento e superfici

Il parser esistente legge `steps[0].road.name` del messaggio AA `0x8006` in `NavView::road`. Il display passa questo valore direttamente a `road_sign::route()` e `draw()`. Non sono stati modificati parser, canali negoziati o gestione Google Maps/Waze.

- Manovre: poligoni sul buffer inattivo `0x1F/0x20`, coordinate `0..220`, `0..157`, poi switch/visibilita esistenti.
- Corsie: Work su `0x23/0x24`, superficie 283 x 40. Punte e separatori sono quelli dello ZIP, con margine superiore di 4 pixel. Consigliate: indici 4/11; altre: 3/2.
- Cartelli/miniature: bitmap 305 x 44 su `0x18/0x19`, tramite C05 TX0 e RLE32 ABGR esistenti. La selezione fissa riguarda disegno e attivazione; in automatico rimane il doppio buffering.
- Cartello predefinito: x=0, y=9, 62 x 26, 15 spazi iniziali sul testo. Pixel esterni ARGB zero. A verde/bianco, SS azzurro/bianco, SP giallo/nero: valori esatti Work.
- Il cartello ha precedenza sulla miniatura sulla stessa superficie. Senza sigla riconosciuta, si usa la miniatura se disponibile; altrimenti gli handle vengono nascosti. Il testo torna senza spazi aggiunti.
- La spaziatura dipende da un payload cartello effettivamente generabile entro il limite, non dal solo riconoscimento della sigla.
- C05 rifiuta payload oltre 4096 byte, inclusi i 10 byte di intestazione. Non tronca o inventa segmentazioni.
- Nome strada: ISO-8859-15 e NUL invariati, massimo 255 byte di testo. Gli eventuali spazi iniziali occupano parte dei 255 byte.
- Barra `0x35`, distanza `0x1B`, unita `0x1C` e contenitore `0x17`: infrastruttura preservata. La barra usa la distanza valida entro il range predefinito di 500 m; la manovra centrale resta indipendente.

Il Work definitivo applica 0.96 alle rotatorie centrali dopo il fit; le miniature seguono la propria API. Non e stato reintrodotto il precedente fattore 0.85. I nomi storici `grafite_oro`, `Gold` e simili non implicano il ripristino della vecchia palette.

I parametri NBT esistenti rimangono riconosciuti. Le impostazioni storiche di segmentazione, spessore e antialias non introducono varianti del disegno definitivo: il risultato grafico segue il codice Work. TX4 rimane predefinito e TX0 selezionabile.

## Configurazione per la prova dei cartelli

I default delle bitmap e dei cartelli rimangono disabilitati. Esempio da inserire manualmente in `nbt-aad.env`, mantenendo gli altri parametri gia utilizzati:

```ini
NBT_CLUSTER=6wb
NBT_HUDTERM=on
NBT_6WB_ROAD_SHIELDS=1
NBT_6WB_ICON_BITMAPS=1
NBT_6WB_ICON_SURFACE=0x18
```

Per l'altro handle sostituire l'ultima riga con `NBT_6WB_ICON_SURFACE=0x19`; per alternarli usare `auto`; per disabilitare entrambe le superfici usare `off`. La selezione esplicita della superficie prevale sul parametro legacy bitmap. Non sono stati modificati file di configurazione dell'utente.

Non occorrono override dei parametri `NBT_6WB_SHIELD_X/Y/WIDTH/HEIGHT/SPACES`: i default sono quelli definitivi Work sopra indicati.

## Verifiche effettivamente eseguite

- 29 test Python 6WB: 28 passati alla prima esecuzione; un falso positivo del controllo lessicale C++98 sulla stringa `"auto"` corretto; quel solo test rieseguito e passato. Nessuna modifica al renderer per correggere il test.
- 14 test Python della diagnostica NAV: tutti passati, inclusi analisi JSONL, persistenza/rotazione tramite modello filesystem e contratti statici del decoder.
- Totale: 43 casi distinti passati. Sono controlli sorgente/dati e modelli Python, non esecuzione del renderer C++ o del daemon.
- Identita diretta con lo ZIP per i 12 file del manifest.
- Makefile/CMake: stessi 54 sorgenti, compreso `CSRCS` C; tutti i percorsi esistono, nessuna duplicazione. Nuovi moduli header-only: nessuna nuova libreria o unita da collegare.
- Inclusioni locali risolte per i 15 file controllati; parentesi/delimitatori controllati lessicalmente in 15 file C++ e nel programma di cattura dell'anteprima.
- Verificato il sottoinsieme C++98 dei moduli Work e della configurazione. Non introdotte dipendenze C++ moderne o nuove librerie esterne. Non equivale a una compilazione GCC 4.4.2.
- Sintassi Python e AST dello script PowerShell di build verificati senza eseguirlo.
- Invariati rispetto al checkpoint: 15 file infrastrutturali controllati direttamente, incluse definizioni AA, `nav_core`, HUDTerminal, risorse, driver 6WA/J29, diagnostica e script di build; invariati anche MOST/DSI.
- Codec bitmap e testo invariati nelle funzioni ancora utilizzate; controllati sorgenti del percorso AA/cartello/spaziatura e limite C05.
- Nessun riferimento produttivo ai generatori rimossi. `git diff --check` superato.

Non eseguiti: test C++/host, `test/verifica.sh` dello ZIP, test media che compilano un harness, generazione anteprima, compilazione QNX, collegamento del binario, installazioni o prove sulla vettura. Nessun eseguibile generato o sostituito. File OEM, acquisizioni e materiale BMW non tracciato non modificati.

## Compilazione manuale e verifiche residue

La procedura gia predisposta resta invariata. In PowerShell:

```powershell
Set-Location -LiteralPath 'C:\Users\memel\BMW-6WB\milestone-7'
.\build-6wb.cmd
```

In `cmd.exe`:

```bat
cd /d C:\Users\memel\BMW-6WB\milestone-7
build-6wb.cmd
```

Questi sono comandi da eseguire manualmente: non sono stati lanciati durante questa integrazione. Lo script esistente usa QNX SDP 6.5 e `gcc_ntoarmv7le_cpp`, produce un percorso univoco sotto `output/6wb-manual/`, conserva il precedente `nbt-aad` e controlla ELF ARM/QNX. Nessun nuovo comando o dipendenza di link richiesto dall'integrazione.

Nessuna incompatibilita bloccante individuata staticamente. Restano da verificare con la compilazione manuale il compilatore/linker effettivi e con la vettura: accettazione/resa dei colori RGB bitmap, trasparenza e sovrapposizione dei piani, posizione fisica del cartello rispetto al testo, efficacia dei 15 spazi e transizioni cartello/miniatura. Gli handle e le dimensioni misurati non dimostrano da soli il compositing o la fedelta cromatica.
