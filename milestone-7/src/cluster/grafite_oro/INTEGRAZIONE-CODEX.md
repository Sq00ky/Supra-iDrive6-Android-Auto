# Integrazione NBT-AAD · renderer finale BMW 6WB

L’archivio contiene i sorgenti finali sotto `src/cluster/` e un patch unico. Il patch è stato applicato alla copia di riferimento dei sorgenti originali. La repository NBT-AAD di destinazione e la vettura devono ancora essere aggiornate.

## Applicazione

Dalla radice della repository NBT-AAD, con il patch estratto in un percorso accessibile:

```sh
git apply --check /percorso/6wb-renderer-final.patch
git apply /percorso/6wb-renderer-final.patch
```

Il patch parte dai sorgenti precedenti al ridisegno. Se il branch ha già integrato parte delle modifiche, confrontare il diff e i singoli file con il contenuto di `src/cluster/` e integrare soltanto i cambiamenti mancanti. La verifica è sensibile anche alle terminazioni di riga. Il pacchetto conserva le terminazioni dei sorgenti esportati.

| Percorso di destinazione | Funzione |
|---|---|
| `src/cluster/grafite_oro/artwork6wb.{cpp,hpp}` | Renderer definitivo delle manovre, delle corsie e delle miniature |
| `src/cluster/grafite_oro/hudterminal_palette.{cpp,hpp}` | Associazione dei ruoli grafici agli indici BMW |
| `src/cluster/grafite_oro/miniature_raster.hpp` | Rasterizzazione della miniatura |
| `src/cluster/hud6wb_kit.cpp` | Codifica dei poligoni e palette dei piani |
| `src/cluster/hud6wb_road_sign.hpp` | Modulo separato A/SS/SP con riconoscimento, raster ARGB e spaziatura |
| `src/cluster/hud6wb_display.cpp` | Composizione del batch bitmap e del testo |
| `src/cluster/hud6wb_profile.hpp`, `src/cluster/hud6wb_config.hpp` | Superfici, dimensioni e configurazione |

I file `artwork_data.inc`, `maneuvers.inc`, il codec RLE32 e gli header di supporto inclusi sono riferimenti della stessa revisione. Il patch identifica esattamente i file da integrare. Preservare eventuali modifiche indipendenti già presenti nella repository di destinazione. Il modulo cartelli è header-only e usa C++98. Il Makefile deve continuare a compilare le unità 6WB esistenti, incluso `hud6wb_codec.cpp`.

## Dati Android Auto e superfici BMW

- Il parser esistente in `nav_msg.hpp` ricava `NavView::road` da `steps[0].road.name` nello stato Android Auto `0x8006`. Il display usa quel valore direttamente.
- Le manovre principali occupano 221 × 158 su `0x1F/0x20`. Le corsie occupano 283 × 40 su `0x23/0x24`, con punte senza gambo e 4 px di margine superiore. Gli indici vettoriali BMW restano nell’intervallo 0–19. Per le corsie consigliate si usano 4 e 11, per le altre 3 e 2. La palette delle manovre è quella dei sorgenti approvati.
- Il cartello usa il codec `bitmap()` / RLE32 esistente e il comando `0xC05`, sulla superficie bitmap 305 × 44 già selezionata tramite `NBT_6WB_ICON_SURFACE` (coppia `0x18/0x19`). I pixel esterni hanno ARGB zero. Il cartello ha precedenza sulla miniatura quando entrambi sono disponibili.
- `0x1A` continua a ricevere il nome strada originale, con spazi iniziali quando il cartello può essere inviato. Senza sigla valida o quando il payload non è rappresentabile, si invia il testo senza spazi aggiunti.
- Abilitare i cartelli con `NBT_6WB_ROAD_SHIELDS=1` insieme alla configurazione bitmap già verificata sulla vettura. Valori iniziali: `NBT_6WB_SHIELD_X=0`, `NBT_6WB_SHIELD_Y=9`, `NBT_6WB_SHIELD_WIDTH=62`, `NBT_6WB_SHIELD_HEIGHT=26` e `NBT_6WB_SHIELD_SPACES=15`.

## Verifiche eseguite per questo export

- Applicazione del patch cumulativo sulla copia dei sorgenti originali e confronto byte per byte del renderer grafico approvato e del kit.
- `bash test/verifica.sh`, con g++, Python 3 e Node.js. Verifica offline di 28.800 casi di rotatoria, 72 campioni di corsia e dei cartelli C++98 con il codec RLE32 originale. Il test dell’anteprima controlla colori, trasparenza, posizione, spaziatura e rimozione.
- Gli stub in `test/` isolano il renderer e il codec dal trasporto. La build completa NBT-AAD per QNX, l’integrazione sul branch effettivo e la verifica in vettura del compositing, della resa degli spazi e delle transizioni con la miniatura restano da eseguire.
